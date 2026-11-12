"""Replay Claude's exact display ROIs, separating mean RGB from weighted luma."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import sys
import threading
import time

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.analysis.render import RenderServer


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def measure(image):
    rgb = np.rint(image.astype(np.float64)/257.)
    mean_rgb = rgb.mean(axis=2)
    weighted = rgb @ np.array([.2126, .7152, .0722])
    chroma = np.stack([rgb[..., 0]-rgb[..., 1], rgb[..., 2]-rgb[..., 1]], axis=2)
    blurred = cv2.GaussianBlur(chroma, (0, 0), 2)
    hp = chroma-blurred
    hp_std = hp.std(axis=(0, 1))
    lp_std = blurred.std(axis=(0, 1))
    return dict(mean_rgb_mean=float(mean_rgb.mean()),
                mean_rgb_hp_std=float((mean_rgb-cv2.GaussianBlur(mean_rgb, (0, 0), 2)).std()),
                rec709_encoded_luma_hp_std=float((weighted-cv2.GaussianBlur(weighted, (0, 0), 2)).std()),
                chroma_rg_bg_hp_std=hp_std.tolist(), chroma_rg_bg_lp_std=lp_std.tolist(),
                chroma_hp_std_mean=float(hp_std.mean()), chroma_lp_std_mean=float(lp_std.mean()))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server', type=Path, required=True)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--reference-proof', type=Path)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    a = p.parse_args()
    os.environ['PATH'] = os.pathsep.join(str(d.resolve()) for d in a.runtime_dir) + os.pathsep + os.environ.get('PATH', '')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    executable_hash = sha256(a.server)
    reference = None
    if a.reference_proof:
        old = json.loads(a.reference_proof.read_text(encoding='utf-8'))
        if old.get('server_sha256') != executable_hash:
            raise ValueError('Reference executable is not the one in the immutable evidence')
        reference = dict(path=str(a.reference_proof), sha256=sha256(a.reference_proof))
    names = ('tests/raw_bench/camera_denoise_exact_roi.py', 'tests/analysis/render.py')
    if reference is None:
        names += ('src/raw/CameraDenoise.cpp', 'src/raw/CameraDenoise.h')
    proof = dict(schema_version=1, created_utc=stamp, label=a.label, server_sha256=executable_hash,
                 reference=reference, sources={n: dict(sha256=sha256(ROOT/n), text=(ROOT/n).read_text(encoding='utf-8')) for n in names},
                 scenes=[], limits=['Crop is measured AFTER full-frame denoising/rendering.',
                                    'Mean RGB is not physical luminance; encoded Rec709 luma is not linear Y.',
                                    'Gaussian highpass/lowpass includes actual texture and scene gradients.',
                                    'No clean truth or visual validation; optics disabled.'])
    failed = False
    for name, x, y in (('7Y6A7599.CR3', 2940, 2940), ('IMG_7958.CR2', 3150, 1470), ('6V1A9539.CR3', 2100, 2940)):
        source = (a.base/name).resolve()
        scene = dict(source=str(source), source_sha256=sha256(source), roi=[x, y, 420, 420], states={})
        proof['scenes'].append(scene)
        server = RenderServer(a.server.resolve(), timeout=10)
        try:
            for label, force in (('B0', dict(luminance=0, chrominance=0)),
                                 ('Bcol50', dict(luminance=0, chrominance=50))):
                guard = threading.Timer(180, server.process.kill)
                guard.start()
                try:
                    start = time.monotonic()
                    pixels, metadata = server.render(source, 'neutral', 0, camera_denoise=force)
                    elapsed = time.monotonic()-start
                    if y+420>pixels.shape[0] or x+420>pixels.shape[1]:
                        raise ValueError('ROI outside actual oriented render')
                    roi = pixels[y:y+420, x:x+420].copy()
                    del pixels
                    scene['states'][label] = dict(metadata=metadata, metrics=measure(roi), request_wall_seconds=elapsed)
                    print(name, label, scene['states'][label]['metrics'], flush=True)
                finally:
                    guard.cancel()
                    guard.join()
        except Exception as exc:
            scene['error'] = repr(exc)
            failed = True
        finally:
            server.close()
        if failed:
            break
    proof['verdict'] = 'fail' if failed else 'measured_not_quality_validation'
    output = ROOT/'docs/validation/raw'/f'camera-denoise-exact-roi-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, proof['verdict'], flush=True)
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
