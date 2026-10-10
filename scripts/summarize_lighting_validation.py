#!/usr/bin/env python3
"""Derive CSV/curves from the lighting audit, excluding uncontrolled LR states."""
import argparse
import csv
import json
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.input.read_text(encoding='utf-8'))
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for feature in ('exposition', 'contraste', 'ombre', 'noir', 'blanc', 'hl'):
        for engine in ('IPR', 'LR'):
            for sign in (-1, 1):
                records = [r for r in data['records'] if r['feature'] == feature and
                           r['engine'] == engine and np.sign(r['value']) == sign and
                           r['controlled_triplet']]
                if not records:
                    continue
                row = {'feature': feature, 'engine': engine, 'value': records[0]['value'],
                       'scenes': len(records)}
                for key in ('delta_y', 'delta_low', 'delta_mid', 'delta_high',
                            'mean_abs_delta_y', 'clipped_pct_variant', 'black_pct_variant',
                            'changed_pct', 'reversed_pct'):
                    vals = [r['measurement'][key] for r in records if key in r['measurement']]
                    if vals:
                        row[key+'_mean'] = float(np.mean(vals))
                        row[key+'_max'] = float(np.max(vals))
                for zone in ('low', 'mid', 'high'):
                    vals = [r['measurement']['ratio_'+zone] for r in records
                            if r['measurement']['ratio_'+zone] is not None]
                    if vals:
                        row['ratio_'+zone+'_median'] = float(np.median(vals))
                rows.append(row)
    fields = list(dict.fromkeys(k for row in rows for k in row))
    with (args.output/'resume-lumieres-camera-neutral-20261009.csv').open('w', encoding='utf-8', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    # Response curves: three sampled states, not interpolated calibration.
    for feature in dict.fromkeys(row['feature'] for row in rows):
        selected = [r for r in rows if r['feature'] == feature]
        span = max(.01, max(abs(r['delta_'+z+'_mean']) for r in selected for z in ('low', 'mid', 'high')))*1.1
        svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="920" height="400" viewBox="0 0 920 400">',
               '<rect width="920" height="400" fill="white"/>',
               f'<text x="20" y="25" font-family="sans-serif" font-size="18">{feature} — variation Y vs neutre (moyenne scènes)</text>']
        for i, zone in enumerate(('low', 'mid', 'high')):
            left = 30+i*300
            svg.extend([f'<text x="{left+80}" y="55" font-family="sans-serif">{zone}</text>',
                        f'<path d="M {left+10} 210 H {left+260} M {left+135} 70 V 345" stroke="#aaa" fill="none"/>',
                        f'<text x="{left+5}" y="370" font-family="sans-serif" font-size="12">−extrême</text>',
                        f'<text x="{left+130}" y="370" font-family="sans-serif" font-size="12">0</text>',
                        f'<text x="{left+215}" y="370" font-family="sans-serif" font-size="12">+extrême</text>',
                        f'<text x="{left+15}" y="85" font-family="sans-serif" font-size="11">+{span:.3f}</text>',
                        f'<text x="{left+15}" y="335" font-family="sans-serif" font-size="11">−{span:.3f}</text>'])
            for engine, color in [('IPR', '#1565c0'), ('LR', '#e65100')]:
                ends = sorted((r for r in selected if r['engine'] == engine), key=lambda r:r['value'])
                if len(ends) != 2:
                    continue
                values = [ends[0]['delta_'+zone+'_mean'], 0, ends[1]['delta_'+zone+'_mean']]
                coords = [(left+20+j*115, 210-v/span*125) for j,v in enumerate(values)]
                svg.append(f'<polyline points="{" ".join(f"{x},{y:.2f}" for x,y in coords)}" fill="none" stroke="{color}" stroke-width="2"/>')
                for x,y in coords:
                    svg.append(f'<circle cx="{x}" cy="{y:.2f}" r="3" fill="{color}"/>')
        svg.extend(['<text x="650" y="25" font-family="sans-serif" fill="#1565c0">IPR</text>',
                    '<text x="710" y="25" font-family="sans-serif" fill="#e65100">LR</text>', '</svg>'])
        (args.output/f'lumieres-{feature}-camera-neutral-20261009.svg').write_text('\n'.join(svg), encoding='utf-8')
    print(json.dumps({'records': len(data['records']), 'missing': data['missing'],
                      'warnings': [{'scene':r['scene'], 'engine':r['engine'], 'feature':r['feature'],
                                    'value':r['value'], 'warnings':r['warnings']} for r in data['records'] if r['warnings']],
                      'summary': rows}, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    main()
