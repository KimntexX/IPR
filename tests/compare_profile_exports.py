#!/usr/bin/env python3
"""Compare explicitly named IPR/LR profile exports without changing photographs."""
import argparse
import json
from datetime import datetime, timezone
from pathlib import Path

from analyse_base_colorimetrique_v2 import audit_tiff, compare, file_hash, strict_region
from analyse_color_mixer import native_pixels, choose_crop


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=Path('tests/image/base/neutre'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--step', type=int, default=8)
    args = parser.parse_args()
    if args.step < 1 or args.output.exists():
        parser.error('Positive sampling step and a new output file are required')
    result = {'schema': 1, 'timestamp_utc': datetime.now(timezone.utc).isoformat(),
              'analyser_sha256': file_hash(Path(__file__)),
              'method': {'encoding': 'native 16-bit sRGB', 'step': args.step,
                         'geometry': '0/1 pixel crop only; no optical registration',
                         'sign': 'Lightroom minus IPR',
                         'scope': 'export agreement, not absolute color accuracy'},
              'pairs': [], 'missing_references': []}
    for ipr_path in sorted(args.input.glob('*-IPR-*-AsShot.tiff')):
        lr_path = ipr_path.with_name(ipr_path.name.replace('-IPR-', '-LR-')).with_suffix('.tif')
        if not lr_path.exists():
            result['missing_references'].append(str(lr_path))
            continue
        ipr_meta, lr_meta = audit_tiff(ipr_path), audit_tiff(lr_path)
        if ipr_meta['orientation'] != 1 or lr_meta['orientation'] != 1:
            raise ValueError(f'Unsupported orientation: {ipr_path}')
        ipr, lr = native_pixels(ipr_path), native_pixels(lr_path)
        crops = choose_crop(lr, ipr, args.step)
        crop = crops[0]
        height, width = ipr.shape[:2]
        coords = [0, 0, width, height]
        measured = compare(strict_region(ipr, coords, args.step),
                           strict_region(lr, coords, args.step, crop['top'], crop['left']))
        result['pairs'].append({'ipr': ipr_meta, 'lr': lr_meta,
                                'crop_candidates': crops, 'global': measured})
        print(ipr_path.name, 'DE00=', round(measured['delta_e00_mean'], 4),
              'MAE/255=', round(measured['rgb_mae_normalized'] * 255, 4), flush=True)
        del ipr, lr
    if not result['pairs']:
        raise ValueError('No matching export pairs')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False, allow_nan=False)


if __name__ == '__main__':
    main()
