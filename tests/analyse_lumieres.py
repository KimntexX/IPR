#!/usr/bin/env python3
"""Audit six lighting controls, native TIFF samples vs each engine's neutral."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np

from analyse_color_mixer import metadata, linearize
from analyse_exposition import audit, neutral_file, pixels, response

FIELDS = {'exposition': 'Exposure2012', 'contraste': 'Contrast2012',
          'ombre': 'Shadows2012', 'noir': 'Blacks2012',
          'blanc': 'Whites2012', 'hl': 'Highlights2012'}


def measure(base, changed, value, feature):
    if base.shape != changed.shape:
        raise ValueError('Within-engine geometry mismatch')
    weights = np.array([.2126, .7152, .0722])
    y0, y1 = linearize(base) @ weights, linearize(changed) @ weights
    delta = y1-y0
    q20, q40, q60, q80 = np.quantile(y0, [.2, .4, .6, .8])
    zones = {'low': y0 <= q20, 'mid': (y0 >= q40) & (y0 <= q60), 'high': y0 >= q80}
    result = {'mean_y_neutral': float(y0.mean()), 'mean_y_variant': float(y1.mean()),
              'delta_y': float(delta.mean()), 'mean_abs_delta_y': float(np.abs(delta).mean()),
              'changed_pct': float(100*(np.abs(delta) > 2e-5).mean()),
              'black_pct_neutral': float(100*(y0 <= .001).mean()),
              'black_pct_variant': float(100*(y1 <= .001).mean()),
              'clipped_pct_neutral': float(100*(base.max(axis=-1) >= 1-1/65535).mean()),
              'clipped_pct_variant': float(100*(changed.max(axis=-1) >= 1-1/65535).mean())}
    for name, mask in zones.items():
        result['delta_'+name] = float(delta[mask].mean())
        result['ratio_'+name] = float(y1[mask].mean()/y0[mask].mean()) if y0[mask].mean() > 0 else None
    if feature != 'contraste':
        result['reversed_pct'] = float(100*((delta*np.sign(value)) < -2e-5).mean())
    if feature == 'exposition':
        result.update(response(base, changed, value))
    return result


def setting_warnings(info, neutral, field, value):
    # Reuse the zero-adjustment audit with the tested control removed.
    adjusted = dict(info)
    adjusted['settings'] = dict(info['settings'])
    if adjusted['settings']:
        adjusted['settings'][field] = '0'
    warnings = audit(adjusted, 0)
    # Optical corrections are allowed if constant within a triplet.
    warnings = [w for w in warnings if not w.startswith(('LensProfileEnable=', 'AutoLateralCA='))]
    if info['settings']:
        try:
            if float(info['settings'][field]) != value:
                warnings.append('Wrong tested value: '+str(info['settings'].get(field)))
        except (KeyError, ValueError):
            warnings.append('Missing tested value')
        keys = set(info['settings']) | set(neutral['settings'])
        differences = [k for k in keys if k not in (field, 'RawFileName')
                       and info['settings'].get(k) != neutral['settings'].get(k)]
        if differences:
            warnings.append('Settings changed besides tested control: '+', '.join(sorted(differences)))
    return warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('tests/image/base'))
    parser.add_argument('--step', type=int, default=8)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.step <= 0:
        parser.error('step must be positive')
    scenes = sorted(p.stem for p in (args.root/'exposition').glob('*.tif') if '-' not in p.stem)
    result = {'step': args.step, 'scenes': scenes, 'records': [], 'neutrals': [], 'missing': [],
              'ipr_provenance': 'filename convention, no settings manifest',
              'zones': 'per-engine neutral Y quantiles: bottom20, middle40-60, top20'}
    for scene in scenes:
        for engine in ('IPR', 'LR'):
            path0 = neutral_file(args.root, scene, engine)
            if not path0.exists():
                result['missing'].append(str(path0))
                continue
            base = pixels(path0, args.step)
            info0 = metadata(path0)
            neutral_warnings = setting_warnings(info0, info0, 'Exposure2012', 0)
            result['neutrals'].append({'scene': scene, 'engine': engine,
                                      'metadata': info0, 'warnings': neutral_warnings})
            for feature, field in FIELDS.items():
                limit = 1 if feature == 'exposition' else 100
                for value, suffix in [(-limit, ''), (limit, '-2')]:
                    stem = scene + ('-IPR' if engine == 'IPR' else '')
                    path = args.root/feature/(stem+suffix+('.tiff' if engine == 'IPR' else '.tif'))
                    if not path.exists():
                        result['missing'].append(str(path))
                        continue
                    info = metadata(path)
                    warnings = neutral_warnings + setting_warnings(info, info0, field, value)
                    record = {'scene': scene, 'engine': engine, 'feature': feature, 'value': value,
                              'metadata': info, 'warnings': warnings,
                              'controlled_triplet': not warnings,
                              'measurement': measure(base, pixels(path, args.step), value, feature)}
                    result['records'].append(record)
                    print(scene, engine, feature, value, 'WARN' if warnings else 'OK', flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
    rows = [{'scene': r['scene'], 'engine': r['engine'], 'feature': r['feature'],
             'value': r['value'], 'controlled_triplet': r['controlled_triplet'],
             'warnings': '; '.join(r['warnings']), **r['measurement']} for r in result['records']]
    fields = list(dict.fromkeys(k for row in rows for k in row)) or ['scene']
    with args.output.with_suffix('.csv').open('w', encoding='utf-8', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print('Missing:', result['missing'])
    print('Audited records:', len(rows), 'warnings:', sum(bool(r['warnings']) for r in rows))


if __name__ == '__main__':
    main()
