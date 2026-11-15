"""N3 smoke: A, signed B0, B50/B100 and replay on three real camera witnesses.

Pixels stay in memory. This measures render differences, NOT noise quality.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.analysis.render import RenderServer


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def difference(a, b):
    # One row group at a time, without a full float64 copy of a 32 MP image.
    total = np.zeros(3, dtype=np.float64)
    absolute = total.copy()
    maximum = 0
    changed = 0
    for y in range(0, a.shape[0], 128):
        delta = a[y:y+128].astype(np.int32) - b[y:y+128].astype(np.int32)
        total += delta.sum(axis=(0, 1))
        absolute += np.abs(delta).sum(axis=(0, 1))
        maximum = max(maximum, int(np.abs(delta).max()))
        changed += int(np.any(delta != 0, axis=2).sum())
    count = a.shape[0] * a.shape[1]
    return dict(mean_delta_u16=(total/count).tolist(), mean_absolute_u16=(absolute/count).tolist(),
                maximum_absolute_u16=maximum, fraction_changed_pixels=changed/count,
                scope='final sRGB uint16 samples, not a sensor-noise metric')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server', type=Path, required=True)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    a = p.parse_args()
    os.environ['PATH'] = os.pathsep.join(str(d.resolve()) for d in a.runtime_dir) + os.pathsep + os.environ.get('PATH', '')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    data = dict(schema_version=1, created_utc=stamp, study='camera_denoise_N3_real_render_smoke',
                revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                server_sha256=sha256(a.server), scenes=[],
                sources={name: dict(sha256=sha256(ROOT/name), text=(ROOT/name).read_text(encoding='utf-8'))
                         for name in ('tests/raw_bench/camera_denoise_witnesses.py', 'tests/analysis/render.py')},
                limits=['Not visual validation, detail preservation or Lightroom equivalence.',
                        'Final sRGB output cannot establish where signed values were clamped.',
                        'Optics are the TestRecipes neutral defaults, not an optically corrected campaign.',
                        'No claim about preview/full-resolution equivalence or whole-system memory.'])
    failed = False
    for name in ('6V1A9153.CR3', 'IMG_3135.CR2', '7Y6A7599.CR3'):
        source = (a.base/name).resolve()
        scene = dict(source=str(source), source_sha256=sha256(source), states={})
        data['scenes'].append(scene)
        server = RenderServer(a.server.resolve(), timeout=10)
        watchdog = threading.Timer(180, server.process.kill)
        watchdog.start()
        try:
            def render(label, force):
                start = time.monotonic()
                pixels, meta = server.render(source, 'neutral', 0, camera_denoise=force)
                scene['states'][label] = dict(metadata=meta, wall_seconds=time.monotonic()-start)
                print(name, label, scene['states'][label]['wall_seconds'], flush=True)
                return pixels
            original = render('A', None)
            zero = render('B0', {'luminance': 0, 'chrominance': 0})
            scene['A_to_B0'] = difference(zero, original)
            del original
            half = render('B50', {'luminance': 50, 'chrominance': 50})
            scene['B0_to_B50'] = difference(half, zero)
            del half
            full = render('B100', {'luminance': 100, 'chrominance': 100})
            scene['B0_to_B100'] = difference(full, zero)
            del full, zero
            repeated = render('B100_replay', {'luminance': 100, 'chrominance': 100})
            del repeated
            repeated = render('A_replay', None)
            del repeated
            scene['pass'] = (scene['states']['A']['metadata']['sha256'] == scene['states']['A_replay']['metadata']['sha256']
                             and scene['states']['B100']['metadata']['sha256'] == scene['states']['B100_replay']['metadata']['sha256']
                             and scene['B0_to_B100']['maximum_absolute_u16'] > 0)
            failed = failed or not scene['pass']
        except Exception as exc:
            scene['error'] = repr(exc)
            failed = True
        finally:
            watchdog.cancel()
            watchdog.join()
            server.close()
        if failed:
            break
    data['verdict'] = 'fail' if failed else 'pass_three_camera_render_smoke_not_visual_validation'
    output = ROOT/'docs/validation/raw'/f'camera-denoise-n3-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, data['verdict'], flush=True)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
