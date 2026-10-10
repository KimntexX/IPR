#!/usr/bin/env python3
"""Read-only EV audit: native 16-bit samples, each engine's own neutral.

Naming contract: unsuffixed variant = -1 EV, suffix -2 = +1 EV.
IPR values are inferred from that contract, not certified by TIFF metadata.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

from analyse_color_mixer import metadata, native_pixels, linearize


def response(neutral, variant, ev):
    if neutral.shape != variant.shape:
        raise ValueError('Within-engine geometry changed')
    base, changed = linearize(neutral), linearize(variant)
    weights = np.array([.2126, .7152, .0722])
    y0, y1 = base @ weights, changed @ weights
    # Avoid black quantisation and baseline/output channel clipping.
    mask = (base.min(axis=-1) > .005) & (base.max(axis=-1) < .45)
    mask &= changed.max(axis=-1) < .98
    ratio = y1[mask] / y0[mask]
    return {
        'samples': int(y0.size), 'unclipped_samples': int(mask.sum()),
        'mean_y_neutral': float(y0.mean()), 'mean_y_variant': float(y1.mean()),
        'mean_ratio': float(y1.mean() / y0.mean()) if y0.mean() > 0 else None,
        'median_ratio_unclipped': float(np.median(ratio)) if ratio.size else None,
        'median_ev_unclipped': float(np.log2(np.median(ratio))) if ratio.size and np.median(ratio) > 0 else None,
        'zero_ratio_samples': int((ratio == 0).sum()),
        'mae_linear_expected_unclipped': float(np.abs(changed[mask] - base[mask] * 2**ev).mean()) if ratio.size else None,
        'direction_reversed_pct': float(100 * ((y1-y0)*ev < -2e-5).mean()),
        'clipped_channel_pct': float(100 * (variant.max(axis=-1) >= 1-1/65535).mean()),
        'black_pct': float(100 * (y1 <= .001).mean()),
    }


def pixels(path, step):
    mapped = native_pixels(path)
    return np.array(mapped[::step, ::step, :3], dtype=np.float64) / np.iinfo(mapped.dtype).max


def neutral_file(root, scene, engine):
    if engine == 'IPR':
        return root / 'neutre' / (scene + '-IPR.tiff')
    preferred = root / 'neutre' / (scene + '-LR-CameraNeutral-AsShot.tif')
    fallback = root / 'neutre' / (scene + '.tif')
    return preferred if preferred.exists() else fallback


def audit(info, ev):
    warnings = []
    if 'srgb' not in info['icc'].lower():
        warnings.append('ICC not explicitly sRGB: linearisation not certified')
    if info['bits'][:3] != [16, 16, 16]:
        warnings.append('Not RGB 16 bit')
    settings = info['settings']
    if settings:
        for key, expected in [('CameraProfile', 'Camera Neutral'), ('WhiteBalance', 'As Shot')]:
            if settings.get(key) != expected:
                warnings.append(f'{key}={settings.get(key)}, expected {expected}')
        for key in ['Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012',
                    'Whites2012', 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze',
                    'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing',
                    'ColorNoiseReduction', 'LensProfileEnable', 'AutoLateralCA']:
            expected = ev if key == 'Exposure2012' else 0
            try:
                if float(settings[key]) != expected:
                    warnings.append(f'{key}={settings[key]}, expected {expected}')
            except (KeyError, ValueError):
                warnings.append(f'{key} absent or invalid')
        if info['active_mixer']:
            warnings.append('Active color mixer: ' + str(info['active_mixer']))
    return warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('tests/image/base'))
    parser.add_argument('--step', type=int, default=8)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.step < 1:
        parser.error('--step must be positive')
    scenes = sorted(p.stem for p in (args.root / 'exposition').glob('*.tif') if '-' not in p.stem)
    result = {'sample_step': args.step, 'naming': {'unsuffixed': -1, '-2': 1},
              'ipr_settings_provenance': 'filename convention; no export manifest supplied',
              'records': [], 'missing': [], 'neutrals': []}
    for scene in scenes:
        for engine in ('IPR', 'LR'):
            ext = '.tiff' if engine == 'IPR' else '.tif'
            stem = scene + ('-IPR' if engine == 'IPR' else '')
            neutral_path = neutral_file(args.root, scene, engine)
            if not neutral_path.exists():
                result['missing'].append(str(neutral_path))
                # Audit available variants even without the EV0 reference.
                for ev, suffix in [(-1, ''), (1, '-2')]:
                    path = args.root / 'exposition' / (stem + suffix + ext)
                    if path.exists():
                        info = metadata(path)
                        result['records'].append({'scene': scene, 'engine': engine, 'ev': ev,
                            'metadata': info, 'warnings': audit(info, ev), 'measurement': None})
                continue
            base = pixels(neutral_path, args.step)
            info0 = metadata(neutral_path)
            result['neutrals'].append({'scene': scene, 'engine': engine,
                'metadata': info0, 'warnings': audit(info0, 0)})
            for ev, suffix in [(-1, ''), (1, '-2')]:
                path = args.root / 'exposition' / (stem + suffix + ext)
                if not path.exists():
                    result['missing'].append(str(path))
                    continue
                info = metadata(path)
                differences = {key: {'neutral': info0['settings'].get(key), 'variant': value}
                    for key, value in info['settings'].items()
                    if key not in ('Exposure2012', 'RawFileName') and value != info0['settings'].get(key)}
                warnings = audit(info, ev)
                if differences:
                    warnings.append('Settings changed besides EV: ' + ', '.join(differences))
                result['records'].append({'scene': scene, 'engine': engine, 'ev': ev,
                    'metadata': info, 'warnings': warnings,
                    'settings_differences_from_neutral': differences,
                    'comparable_to_neutral': not warnings,
                    'measurement': response(base, pixels(path, args.step), ev)})
                print(scene, engine, ev, result['records'][-1]['measurement'], flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    rows = [{'scene': r['scene'], 'engine': r['engine'], 'ev': r['ev'],
             **r['measurement']} for r in result['records'] if r['measurement']]
    with args.output.with_suffix('.csv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]) if rows else ['scene', 'engine', 'ev'])
        writer.writeheader()
        writer.writerows(rows)
    print('Missing:', result['missing'])
    for r in result['records'] + result['neutrals']:
        if r['warnings']:
            print('AUDIT', r['scene'], r['engine'], r.get('ev', 0), r['warnings'])


if __name__ == '__main__':
    main()
