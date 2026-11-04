"""Paired descriptive comparison; no new threshold, tuning or model selection."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare(previous, current):
    required = 'completed_fine_signal_regression_not_photographic_qualification'
    if any(r['status'] != required for r in (previous, current)):
        raise ValueError('Complete known fine regressions required')
    for key in ('cases', 'fields', 'criteria'):
        if previous['plan'][key] != current['plan'][key]:
            raise ValueError('Fine inputs, noise or criteria differ: '+key)
    identity = lambda r: tuple(r[k] for k in ('seed', 'case_index', 'phase_index', 'noise_index', 'replicate'))
    def indexed(rows):
        mapping = {identity(r): r for r in rows}
        if len(mapping) != len(rows):
            raise ValueError('Duplicate observations')
        return mapping
    old, new = indexed(previous['records']), indexed(current['records'])
    if len(old) != 4032 or set(old) != set(new):
        raise ValueError('Observation coverage differs')
    for key, before in old.items():
        after = new[key]
        for field in ('target_sha256', 'observation_sha256', 'sigma_sha256'):
            if before[field] != after[field]:
                raise ValueError('Unpaired measurement: '+field)
        if abs(before['parent']['rgb_mse']-after['parent']['rgb_mse']) > 1e-9:
            raise ValueError('Frozen reconstruction reference differs')
    strong = previous['plan']['criteria']['strong_amplitude']
    result = dict(previous_passed=previous['pilot_gates_passed'], current_passed=current['pilot_gates_passed'],
        observation_count=len(old), comparisons=[])
    for seed in (71, 73):
        for level in (1, 2):
            pairs = [(old[k], new[k]) for k in sorted(old) if k[0] == seed and k[3] == level]
            if not pairs:
                raise ValueError('Missing seed/noise group')
            summed = lambda i: sum(pair[i]['full']['rgb_mse'] for pair in pairs)
            groups = [[g for g in r['groups'] if g['seed'] == seed and g['noise_index'] == level
                and g['amplitude'] == strong] for r in (previous, current)]
            if any(len(group) != 12 for group in groups):
                raise ValueError('Missing strong-texture group')
            result['comparisons'].append(dict(seed=seed, noise_index=level,
                current_over_previous_rgb_mse=summed(1)/summed(0),
                previous_strong_failed=sum(not g['strong_passed'] for g in groups[0]),
                current_strong_failed=sum(not g['strong_passed'] for g in groups[1]),
                previous_minimum_transfer=min(min(g['gain_over_parent']) for g in groups[0]),
                current_minimum_transfer=min(min(g['gain_over_parent']) for g in groups[1])))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--current', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = compare(json.loads(args.previous.read_text(encoding='utf-8')),
        json.loads(args.current.read_text(encoding='utf-8')))
    result.update(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
        previous_sha256=sha256(args.previous), current_sha256=sha256(args.current),
        tool_sha256=sha256(Path(__file__)), status='completed_paired_known_fine_comparison_not_qualification')
    with args.output.open('x', encoding='utf-8') as handle:
        json.dump(result, handle, indent=2, allow_nan=False)
    print(json.dumps(result['comparisons']), flush=True)
