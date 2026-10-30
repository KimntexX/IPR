"""Rebuild and verify frozen Lightroom dark masks, then audit native full-frame denoising."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import cv2
import numpy as np


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for block in iter(lambda: handle.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def frozen_dark_mask(reference, scene):
    if reference is None or reference.dtype != np.uint16 or reference.ndim != 3 or reference.shape[2] < 3:
        raise ValueError('Reference must be a readable uint16 RGB TIFF')
    area = scene['masks']['common_area']
    x, y = area['lr_origin_xy']
    width, height = area['size_wh']
    crop = reference[y:y+height, x:x+width]
    if crop.shape[:2] != (height, width):
        raise ValueError('Reference common area outside image')
    rgb = cv2.cvtColor(crop[..., :3], cv2.COLOR_BGR2RGB).astype(np.float32)/257.
    luma = .2126*rgb[..., 0] + .7152*rgb[..., 1] + .0722*rgb[..., 2]
    smooth = cv2.GaussianBlur(luma, (0, 0), 1.5)
    gradient = np.hypot(cv2.Sobel(smooth, cv2.CV_32F, 1, 0), cv2.Sobel(smooth, cv2.CV_32F, 0, 1))
    dark = (luma < 25) & (gradient < np.percentile(gradient, 90))
    checksum = hashlib.sha256(np.packbits(dark).tobytes()).hexdigest()
    if checksum != scene['masks']['sha256_packbits']['dark']:
        raise ValueError(f'Frozen dark mask hash mismatch: {checksum}')
    if int(dark.sum()) != scene['masks']['pixels']['dark']:
        raise ValueError('Frozen dark mask count mismatch')
    ipr_width, ipr_height = scene['alignment']['ipr_size_wh']
    ix, iy = area['ipr_origin_xy']
    if ix < 0 or iy < 0 or ix+width > ipr_width or iy+height > ipr_height:
        raise ValueError('IPR common area outside image')
    mask = np.zeros((ipr_height, ipr_width), np.uint8)
    mask[iy:iy+height, ix:ix+width] = dark
    return mask, checksum


def coarse_dark_cells(reference, scene):
    area=scene['masks']['common_area']
    x,y=area['lr_origin_xy'];width,height=area['size_wh']
    ix,iy=area['ipr_origin_xy']
    ipr_width,ipr_height=scene['alignment']['ipr_size_wh']
    mask=np.zeros((ipr_height,ipr_width),np.uint8)
    cells=[]
    for row in range(0,height-127,128):
        for col in range(0,width-127,128):
            bgr=reference[y+row:y+row+128,x+col:x+col+128,:3].astype(np.float32)/257.
            luma=.2126*bgr[...,2]+.7152*bgr[...,1]+.0722*bgr[...,0]
            mean=float(luma.mean())
            if mean<25:
                rect=[ix+col,iy+row,128,128]
                mask[rect[1]:rect[1]+128,rect[0]:rect[0]+128]=1
                cells.append(dict(camera_roi=rect,lr_mean_luma=mean))
    return mask,cells


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--project-root', type=Path, required=True)
    p.add_argument('--profile-root', type=Path, required=True)
    p.add_argument('--reference-proof', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    p.add_argument('--coarse-cells', action='store_true', help='All pixels of fixed 128px cells with Lightroom mean luma <25; no pixel threshold.')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    reference = json.loads(args.reference_proof.read_text(encoding='utf-8'))
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH', '')
    cv2.setNumThreads(1)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    names = ['tests/raw_bench/audit_dark_mean.py', 'tests/raw_bench/camera_denoise_leakage.cpp',
             'src/raw/CameraDenoise.h', 'src/raw/CameraDenoise.cpp',
             'src/profiles/CameraProfileEngine.cpp', 'src/core/WorkingColorSpace.h']
    proof = dict(schema_version=1, created_utc=stamp, study='coarse_dark_cells_mean_audit' if args.coarse_cells else 'fixed_dark_mask_mean_audit',
                 revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 reference_proof_sha256=sha256(args.reference_proof), tool_sha256=sha256(args.tool),
                 sources={v: dict(sha256=sha256(root/v)) for v in names},
                 script_source=Path(__file__).read_text(encoding='utf-8'),
                 versions=dict(numpy=np.__version__, opencv=cv2.__version__), scenes=[],
                 limits=['Same dark mask pixels as the frozen proof, not necessarily identical engine to Claude head.',
                         'Full-frame filter before mask selection; DCP pointwise on selected pixels.',
                         'Mean-matching is a counterfactual, not a production correction or DC ablation.',
                         'No profile-table intermediate captures, no clean signal oracle, optics off.',
                         'Raw/LR hashes and packed mask hashes must match; mismatch stops the campaign.'])
    failed = False
    try:
        with tempfile.TemporaryDirectory(prefix='dark-mean-', dir=root/'build-patch_raw_bench') as scratch:
            for name in ('7Y6A7599', '6V1A9153', 'IMG_3135'):
                scene = reference['scenes'][name]
                lr = args.project_root/scene['lr_neutral']['file']
                raw = args.project_root/scene['raw']['file']
                record = dict(scene=name, lr_file=str(lr), raw_file=str(raw))
                proof['scenes'].append(record)
                if sha256(lr) != scene['lr_neutral']['sha256'] or sha256(raw) != scene['raw']['sha256']:
                    raise ValueError(f'{name}: RAW/LR hash mismatch')
                reference_image=cv2.imread(str(lr), cv2.IMREAD_UNCHANGED)
                mask, packed_hash = frozen_dark_mask(reference_image, scene)
                rectangles_path=None
                if args.coarse_cells:
                    mask, cells=coarse_dark_cells(reference_image, scene)
                    if not cells:
                        record.update(status='no_complete_128px_dark_cells',mask_packed_sha256=packed_hash)
                        del reference_image,mask
                        print(name,'no complete dark cells; no retuning',flush=True)
                        continue
                    rectangles_path=Path(scratch)/f'{name}-cells.json'
                    rectangles_path.write_text(json.dumps([c['camera_roi'] for c in cells]),encoding='utf-8')
                    record.update(cells=cells, cell_selection='Complete nonoverlapping 128x128 cells from common-area origin; LR mean luma<25; all cell pixels; partial edge cells excluded')
                del reference_image
                mask_path = Path(scratch)/f'{name}.bin'
                mask.tofile(mask_path)
                record.update(mask_packed_sha256=packed_hash, mask_bytes_sha256=sha256(mask_path),
                              mask_pixels=int(np.count_nonzero(mask)))
                del mask
                command = [str(args.tool.resolve()), '--input', str(raw),
                           '--manifest', str(root/'config/raw-normalization.json'),
                           '--profile-root', str(args.profile_root.resolve()), '--label', 'fixed-dark-mean',
                           '--mean-audit', '--dark-mask', str(mask_path)]
                if rectangles_path is not None:
                    command.extend(['--cell-rects',str(rectangles_path)])
                result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True,
                                        encoding='utf-8', errors='replace', timeout=180)
                record.update(argv=command, returncode=result.returncode, stderr=result.stderr)
                if result.returncode:
                    record['stdout'] = result.stdout
                    raise RuntimeError(f'{name}: native audit failed')
                record['measurements'] = json.loads(result.stdout)
                if record['measurements']['mask_sha256'] != record['mask_bytes_sha256']:
                    raise ValueError('Native mask bytes hash mismatch')
                print(name, 'measured', flush=True)
    except Exception as exc:
        proof['error'] = f'{type(exc).__name__}: {exc}'
        failed = True
    proof['verdict'] = 'fail' if failed else 'measured_not_quality_validation'
    study='coarse-dark-cells' if args.coarse_cells else 'fixed-dark-mean'
    if args.coarse_cells:
        proof['limits'][0]='Coarse cells selected from mean Lightroom luma of the same RAW, not statistically independent; fine mask verified but not used for measurement.'
    output = root/'docs/validation/raw'/f'camera-denoise-{study}-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, proof['verdict'], proof.get('error', ''), flush=True)
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
