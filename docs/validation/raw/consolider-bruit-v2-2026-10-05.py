"""Consolidate immutable noise measurements, not source photographs.

Run from repository root. Refuses to overwrite its derived JSON evidence.
"""
import hashlib
import json
from pathlib import Path

raw = Path('docs/validation/raw')
names = ('mesure-bruit-v2-initial-2026-10-05-v1.json',
         'mesure-bruit-v2-etendu-2026-10-05-v1.json')
sources = [json.loads((raw / name).read_text(encoding='utf-8')) for name in names]
old_name = 'mesure-bruit-2026-10-04-v4.json'
old = json.loads((raw / old_name).read_text(encoding='utf-8'))
output = raw / 'synthese-bruit-v2-2026-10-05-v1.json'
if output.exists():
    raise SystemExit(f'Refusing to overwrite {output}')
result = {'source_sha256': {name: hashlib.sha256((raw / name).read_bytes()).hexdigest()
                           for name in (*names, old_name)},
          'counts': {'scenes': 0, 'neutrals': 0, 'standard_variants': 0, 'extra_variants': 0,
                     'missing_LR_variants': 0, 'other_audit_issues': 0},
          'series': {}}
for doc in sources:
    counts = result['counts']
    counts['scenes'] += len(doc['scenes'])
    counts['neutrals'] += sum(len(s['neutrals']) for s in doc['scenes'].values())
    counts['missing_LR_variants'] += sum(p['type'] == 'missing_variant' and p['engine'] == 'LR' for p in doc['issues'])
    counts['other_audit_issues'] += sum(p['type'] != 'missing_variant' for p in doc['issues'])
    for image_id, groups in doc['results'].items():
        for key, group in groups.items():
            all_states = group['variants']
            states = [v for v in all_states if not v.get('extra_state')]
            counts['standard_variants'] += len(states)
            counts['extra_variants'] += len(all_states) - len(states)
            metric = 'flat_sigma_Y_ratio' if key.endswith('luminance') else 'flat_sigma_chroma_ratio'
            ratios = [v['summary'][metric] for v in states]
            entry = {'values': [v['value'] for v in all_states],
                     'sigma_ratios': [v['summary'][metric] for v in all_states],
                     'standard_strictly_decreasing': all(a > b for a, b in zip(ratios, ratios[1:])),
                     'mean_Y_drift_percent': [100*(v['summary']['flat_mean_Y_ratio']-1) for v in all_states],
                     'detail_edge_gradient_ratios': [v['summary']['detail_edge_gradient_ratio'] for v in all_states],
                     'all_states_protocol_valid': all(v['protocol_valid'] for v in all_states),
                     'all_states_differ_from_neutral_on_ROIs': all(not v['summary']['all_regions_identical'] for v in all_states),
                     'min_local_mean_Y_drift_percent': min(100*(p['ratios']['mean_Y']-1)
                         for v in all_states for p in v['patches'] if p['ratios']['mean_Y'] is not None),
                     'max_local_mean_Y_drift_percent': max(100*(p['ratios']['mean_Y']-1)
                         for v in all_states for p in v['patches'] if p['ratios']['mean_Y'] is not None)}
            if image_id in old['results']:
                before = old['results'][image_id][key]
                entry['before_after_same_neutral_sha256'] = before['baseline_sha256'] == group['baseline_sha256']
                entry['before_after_same_ROIs'] = before['regions'] == group['regions']
                entry['before_100'] = before['variants'][-1]['summary']
            result['series'][f'{image_id}/{key}'] = entry
output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
print(json.dumps(result['counts']))
ipr = {key: entry for key, entry in result['series'].items() if '/IPR-' in key}
print('IPR strict standard series:', sum(e['standard_strictly_decreasing'] for e in ipr.values()), '/', len(ipr))
print('Maximum absolute median Y drift%:', max(abs(value) for e in ipr.values() for value in e['mean_Y_drift_percent']))
for key, e in ipr.items():
    print(key, 'local Y drift% range', round(e['min_local_mean_Y_drift_percent'], 4), round(e['max_local_mean_Y_drift_percent'], 4))
