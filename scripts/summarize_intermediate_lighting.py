#!/usr/bin/env python3
"""Publish complete zone provenance and derive curves from an immutable run."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests'))
from analyse_color_mixer import linearize
from analyse_exposition import pixels
from analyse_lumieres import setting_warnings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    args = parser.parse_args()
    data = json.loads(args.source.read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    campaign = 'mesure-lumieres-intermediaires-v002-' + now.strftime('%Y%m%dT%H%M%SZ')
    output = args.source.parent/(campaign+'.json')
    data.update(campaign_id=campaign, data_version=2, measured_at_utc=now.isoformat(),
                supersedes_campaign_id=data['campaign_id'],
                revision_reason='Add exact zone counts/quantile boundaries and neutral audit before cleanup',
                finalizer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    zones = {}
    for neutral in data['neutrals']:
        rgb = pixels(Path(neutral['metadata']['path']), data['step'])
        y = linearize(rgb) @ np.array([.2126, .7152, .0722])
        q20, q40, q60, q80 = np.quantile(y, [.2, .4, .6, .8])
        zones[neutral['scene'], neutral['engine']] = dict(
            sampled_pixels=int(y.size), quantiles_Y=[float(v) for v in (q20,q40,q60,q80)],
            zone_counts={'low':int((y<=q20).sum()), 'mid':int(((y>=q40)&(y<=q60)).sum()),
                         'high':int((y>=q80).sum())})
        neutral['sampling'] = zones[neutral['scene'], neutral['engine']]
        neutral['warnings'] = setting_warnings(neutral['metadata'], neutral['metadata'], 'Exposure2012', 0)
    for record in data['records']:
        record['sampling'] = zones[record['scene'], record['engine']]
    with output.open('x', encoding='utf-8') as target:
        json.dump(data, target, indent=2, ensure_ascii=False, allow_nan=False)
        target.write('\n')
    folder = Path('docs/validation/metrics')/campaign
    folder.mkdir(parents=True, exist_ok=False)
    rows = []
    for feature in ('ombre', 'hl'):
        for engine in ('IPR','LR'):
            for value in sorted({r['value'] for r in data['records'] if r['feature']==feature}):
                records = [r for r in data['records'] if (r['feature'],r['engine'],r['value'])==(feature,engine,value)]
                row = dict(campaign_id=campaign, feature=feature, engine=engine, value=value, scenes=len(records))
                for key in ('delta_low','delta_mid','delta_high','clipped_pct_variant','reversed_pct'):
                    row[key] = float(np.mean([r['measurement'][key] for r in records]))
                rows.append(row)
    with (folder/'summary.csv').open('x',newline='',encoding='utf-8') as target:
        writer=csv.DictWriter(target,fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    for feature in ('ombre','hl'):
        selected=[r for r in rows if r['feature']==feature]
        svg=['<svg xmlns="http://www.w3.org/2000/svg" width="840" height="390" viewBox="0 0 840 390">',
             '<rect width="840" height="390" fill="white"/>',
             f'<text x="40" y="25">{feature}: mean linear Y delta (equal weight per scene)</text>',
             '<path d="M60 45 V330 H640" stroke="black" fill="none"/>']
        maxabs=max(abs(r[k]) for r in selected for k in ('delta_low','delta_mid','delta_high')) or 1
        for engine in ('IPR','LR'):
            for index,(key,color) in enumerate(zip(('delta_low','delta_mid','delta_high'),('#237a44','#c46b00','#2864b8'))):
                points=[(r['value'],r[key]) for r in selected if r['engine']==engine]+[(0,0)]
                coords=' '.join(f'{60+(v+100)*2.9:.2f},{187-d/maxabs*130:.2f}' for v,d in sorted(points))
                dashed=' stroke-dasharray="6 4"' if engine=='LR' else ''
                svg.append(f'<polyline points="{coords}" stroke="{color}" fill="none"{dashed}/>')
                svg.append(f'<text x="655" y="{70+index*28+(110 if engine=="LR" else 0)}" fill="{color}">{engine} {key}</text>')
        svg.extend(['<text x="60" y="360">-100</text><text x="340" y="360">0</text><text x="610" y="360">+100</text>',
                    f'<text x="60" y="380">Y scale: +/-{maxabs:.5f}; solid IPR, dashed LR; not speed benchmarks</text>', '</svg>'])
        (folder/(feature+'.svg')).write_text('\n'.join(svg)+'\n',encoding='utf-8')
    print('CANONICAL',output,flush=True)
    print(json.dumps(rows,indent=2),flush=True)
    print('MISSING',data['missing'],'WARNINGS',sum(bool(r['warnings']) for r in data['records']),flush=True)


if __name__=='__main__':
    main()
