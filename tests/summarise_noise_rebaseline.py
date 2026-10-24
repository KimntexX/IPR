"""Summarise paired developed-noise campaigns before and after a demosaic change.

This is a derived comparison, not clean-image ground truth. Native pair records
remain authoritative. Equal weight is given to each eligible pair/brightness bin;
bins from the same pair are not statistically independent.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import statistics


BANDS = ('haar_hh', 'fine', 'mid', 'coarse', 'pixel')
PLANES = ('y', 'co', 'cg')


def distribution(values):
    values = sorted(x for x in values if math.isfinite(x))
    if not values:
        return {'count': 0}
    # Nearest-rank quartiles, without interpolation.
    return {'count': len(values), 'median': statistics.median(values),
            'q1': values[max(0, math.ceil(len(values) * .25) - 1)],
            'q3': values[max(0, math.ceil(len(values) * .75) - 1)]}


def observations(campaign, maximum_drift):
    rows, excluded = {}, []
    for pair in campaign['pairs']:
        drift = pair['mean_y_drift_a_vs_b']
        if not math.isfinite(drift) or abs(drift) > maximum_drift:
            excluded.append({'a': pair['a'], 'b': pair['b'], 'drift': drift})
            continue
        identity = (pair['a_sha256'], pair['b_sha256'])
        for level in pair['bins']:
            for plane in PLANES:
                for band in BANDS:
                    record = level['planes'].get(plane, {}).get(band)
                    if record is None:
                        continue
                    value = record.get('truth_median', 0)
                    if value <= 0 or not math.isfinite(value):
                        continue
                    key = (*identity, level['bin'], plane, band)
                    if key in rows:
                        raise ValueError('Duplicate source-pair/bin/plane/band observation')
                    rows[key] = {'truth': value, 'blind_q25': record['blind_q25'],
                                 'a': pair['a'], 'b': pair['b']}
    return rows, excluded


def compare(before, after, maximum_drift=.02):
    old, excluded_old = observations(before, maximum_drift)
    new, excluded_new = observations(after, maximum_drift)
    summary = {}
    for plane in PLANES:
        bands = {}
        for band in BANDS:
            keys = [k for k in old.keys() & new.keys() if k[-2:] == (plane, band)]
            spectral_old, spectral_new, bias_old, bias_new, ratios = [], [], [], [], []
            for key in sorted(keys):
                ratios.append(new[key]['truth'] / old[key]['truth'])
                bias_old.append(old[key]['blind_q25'] / old[key]['truth'])
                bias_new.append(new[key]['blind_q25'] / new[key]['truth'])
                haar = (*key[:-1], 'haar_hh')
                if haar in old and haar in new:
                    spectral_old.append(old[key]['truth'] / old[haar]['truth'])
                    spectral_new.append(new[key]['truth'] / new[haar]['truth'])
            bands[band] = {'after_over_before_sigma': distribution(ratios),
                           'before_band_over_haar': distribution(spectral_old),
                           'after_band_over_haar': distribution(spectral_new),
                           'before_blind_q25_over_truth': distribution(bias_old),
                           'after_blind_q25_over_truth': distribution(bias_new)}
        summary[plane] = bands
    return {'maximum_absolute_pair_exposure_drift': maximum_drift,
            'excluded_before': excluded_old, 'excluded_after': excluded_new,
            'matched_observations': len(old.keys() & new.keys()),
            'weighting': 'equal pair/bin observations; bins are not independent',
            'summary': summary}


def compare_by_iso(before, after, audit):
    """Use actual metadata, never the folder's nominal ISO."""
    iso_by_hash = {frame['sha256']: frame['metadata']['iso']
                   for group in audit['groups'] for frame in group['frames']}
    groups = {}
    for iso in sorted(set(iso_by_hash.values())):
        def select(campaign):
            return {'pairs': [pair for pair in campaign['pairs']
                              if iso_by_hash.get(pair['a_sha256']) == iso
                              and iso_by_hash.get(pair['b_sha256']) == iso]}
        old, new = select(before), select(after)
        if old['pairs'] and new['pairs']:
            groups[str(iso)] = compare(old, new)
    return groups


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--before', type=Path, required=True)
    parser.add_argument('--after', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-version', type=int, required=True)
    parser.add_argument('--audit', type=Path, required=True, help='metadata/identity audit of the same source pairs')
    args = parser.parse_args(argv)
    if args.output.exists():
        raise SystemExit('Output exists; nothing overwritten')
    data, inputs = [], []
    for path in (args.before, args.after):
        content = path.read_bytes()
        data.append(json.loads(content))
        inputs.append({'file': path.name, 'sha256': hashlib.sha256(content).hexdigest(),
                       'commit': data[-1]['commit'], 'probe': data[-1]['probe']})
    stamp = datetime.now(timezone.utc)
    result = {'campaign_id': f"synthese-bruit-rcd-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}",
              'schema_version': 1, 'data_version': args.data_version,
              'measured_at_utc': stamp.isoformat(),
              'protocol': 'docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md',
              'study': 'matched-source paired-noise demosaic rebaseline',
              'runner': {'file': Path(__file__).name,
                         'sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},
              'inputs': inputs, 'primary': compare(*data),
              'sensitivity_without_drift_exclusions': compare(*data, maximum_drift=math.inf),
              'limitations': ['Difference of exposures is a noise proxy, not a clean image',
                              'Quiet-tile membership may change with the demosaic',
                              'Only matched source hashes and brightness bins are compared',
                              'Nonpositive or nonfinite truth MAD values are excluded from ratios',
                              'No spatial registration or exposure correction is introduced'],
              'units': {'sigma': 'linear Rec.2020 plane sample units', 'ratios': 'dimensionless',
                        'exposure_drift': 'relative mean Y difference, not EV'}}
    audit_bytes = args.audit.read_bytes()
    result['audit'] = {'file': args.audit.name, 'sha256': hashlib.sha256(audit_bytes).hexdigest()}
    result['per_iso'] = compare_by_iso(*data, json.loads(audit_bytes))
    # JSON has no infinity literal; None denotes the unbounded sensitivity pass.
    result['sensitivity_without_drift_exclusions']['maximum_absolute_pair_exposure_drift'] = None
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(args.output)
    print(json.dumps(result['primary']['summary'], indent=2))


if __name__ == '__main__':
    main()
