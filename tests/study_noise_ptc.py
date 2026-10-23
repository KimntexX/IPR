"""Exploratory effective variance fits from immutable pair-audit JSON, not production calibration."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import numpy as np
import scipy
from scipy.optimize import nnls


def fit(points, nonnegative=True):
    x = np.array([p['signal_dn'] for p in points], dtype=float)
    y = np.array([p['variance_dn2'] for p in points], dtype=float)
    if len(x) < 2 or not np.isfinite(x).all() or not np.isfinite(y).all() or np.ptp(x) < 1e-9:
        raise ValueError('Insufficient finite signal range')
    ids = [p['pair_id'] for p in points]
    weights = np.array([1 / ids.count(k) for k in ids])
    scale = max(float(x.max()), 1)
    design = np.column_stack((x / scale, np.ones_like(x)))
    a = design * np.sqrt(weights[:, None])
    b = y * np.sqrt(weights)
    parameters = nnls(a, b)[0] if nonnegative else np.linalg.lstsq(a, b, rcond=None)[0]
    prediction = design @ parameters
    denominator = max(float(np.average(y ** 2, weights=weights)), 1e-20)
    return dict(a_dn_per_signal_dn=float(parameters[0] / scale), b_dn2=float(parameters[1]),
                normalized_rmse=float(np.sqrt(np.average((prediction - y) ** 2, weights=weights) / denominator)),
                intercept_at_boundary=bool(nonnegative and parameters[1] <= 1e-12),
                signal_range_dn=[float(x.min()), float(x.max())])


def analyze(audit):
    results = []
    for group in audit['groups']:
        frames = {f['path']: f for f in group['frames']}
        # Prefer candidates with most admissible measurements; retain disjoint files.
        candidates = [p for p in group['pair_candidates'] if p['status'] != 'rejected'
                      and not p['both_dark_like_not_confirmed']
                      and not p['declared_dark_pair'] and not p['mixed_capture_roles']]
        candidates.sort(key=lambda p: (-sum(m['status'] == 'candidate_stable' for m in p['measurements']), p['left'], p['right']))
        used, selected, exclusions = set(), [], []
        for pair in candidates:
            if (pair['left'] in used or pair['right'] in used
                    or frames[pair['left']]['sha256'] in used or frames[pair['right']]['sha256'] in used):
                exclusions.append(dict(left=pair['left'], right=pair['right'], reason='shared file with retained pair'))
            elif not frames[pair['left']]['folder_iso_matches'] or not frames[pair['right']]['folder_iso_matches']:
                exclusions.append(dict(left=pair['left'], right=pair['right'], reason='folder/ISO mismatch'))
            else:
                selected.append(pair)
                used.update((pair['left'], pair['right'], frames[pair['left']]['sha256'], frames[pair['right']]['sha256']))
        darks = [f for f in frames.values() if f['declared_dark'] and f['folder_iso_matches']
                 and f['metadata'].get('exposure_s') == .005]
        dark_pairs = [p for p in group['pair_candidates'] if p['declared_dark_pair']
                      and p['matching_exposure_metadata'] and p['matching_geometry']
                      and all(frames[path]['metadata'].get('exposure_s') == .005
                              for path in (p['left'], p['right']))]
        for plane_index, offset in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
            record = dict(folder=group['folder'], offset=list(offset),
                          plane=('R', 'G1', 'G2', 'B')[plane_index],
                          dark_paths=[f['path'] for f in darks], excluded_pairs=exclusions)
            if not darks:
                record.update(status='excluded_no_same_iso_dark_candidate', points=[])
                results.append(record)
                continue
            dark_measurements = [m for p in dark_pairs for m in p['measurements']
                                 if m['offset'] == list(offset) and m['status'] == 'candidate_stable']
            if len(dark_measurements) < 3:
                record.update(status='excluded_no_stable_declared_dark_pair', points=[])
                results.append(record)
                continue
            black = float(np.median([r['planes'][plane_index]['mean_dn'] for f in darks for r in f['rois']]))
            points, excluded = [], []
            for pair in selected:
                for m in pair['measurements']:
                    if m['offset'] != list(offset):
                        continue
                    item = dict(pair_id=pair['left'] + ' | ' + pair['right'], roi=m['roi'],
                                signal_dn=m['signal_mean_dn'] - black,
                                variance_dn2=m['noise_variance_dn2'])
                    if m['status'] != 'candidate_stable' or item['signal_dn'] <= 0:
                        excluded.append(dict(**item, reason=m['status'] if m['status'] != 'candidate_stable' else 'nonpositive signal'))
                    else:
                        points.append(item)
            ids = sorted(set(p['pair_id'] for p in points))
            record.update(empirical_black_dn=black, points=points, excluded_points=excluded,
                          independent_pairs=len(ids), dark_identity_confirmed=False,
                          dark_identity_source='user-declared blacks folder, not independently certified',
                          dark_pair_measurements=dark_measurements,
                          dark_variance_dn2=float(np.median([m['noise_variance_dn2'] for m in dark_measurements])),
                          dark_decoder_black_dn=[f['black'][int(f['cfa'][offset[0]][offset[1]])] for f in darks])
            if len(ids) < 3:
                record.update(status='insufficient_pairs_for_exploratory_fit')
            else:
                constrained = fit(points)
                unconstrained = fit(points, nonnegative=False)
                folds = []
                for pid in ids:
                    train = [p for p in points if p['pair_id'] != pid]
                    held = [p for p in points if p['pair_id'] == pid]
                    model = fit(train)
                    errors = [abs(model['a_dn_per_signal_dn'] * p['signal_dn'] + model['b_dn2'] - p['variance_dn2']) /
                              max(p['variance_dn2'], 1e-12) for p in held]
                    folds.append(dict(held_pair=pid, model=model, held_relative_mae=float(np.mean(errors))))
                record.update(status='exploratory_only', constrained=constrained, unconstrained=unconstrained,
                              leave_one_pair_out=folds,
                              loo_relative_mae=float(np.mean([f['held_relative_mae'] for f in folds])),
                              loo_slope_range=[min(f['model']['a_dn_per_signal_dn'] for f in folds),
                                               max(f['model']['a_dn_per_signal_dn'] for f in folds)])
            results.append(record)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path, help='Previous immutable fit campaign')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refusing overwrite')
    source = args.audit.read_bytes()
    audit = json.loads(source)
    previous = json.loads(args.previous.read_bytes()) if args.previous else None
    now = datetime.now(timezone.utc)
    result = dict(campaign_id='noise-ptc-exploratory-v003-' + now.strftime('%Y%m%dT%H%M%SZ'),
                  schema_version=1, data_version=3, measured_at_utc=now.isoformat(),
                  supersedes_campaign_id=previous['campaign_id'] if previous else None,
                  revision_reason='Use shared declared blacks and actual ISO assignment; keep previous campaigns immutable',
                  audit_campaign_id=audit['campaign_id'], audit_sha256=hashlib.sha256(source).hexdigest(),
                  source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  configuration=dict(model='variance DN^2 = a*signal DN+b; nonnegative least squares and unconstrained diagnostic',
                                     weighting='equal total weight per disjoint pair, equal ROI weights within pair',
                                     reference_black='median CFA/ROI means of same-ISO user-declared blacks at 1/200s; require stable dark pair per plane; dark variance diagnostic not forced as intercept',
                                     validation='leave one entire pair out; not an independent unseen dataset',
                                     min_pairs=3, units='decoder DN and DN^2; no conversion to electrons or normalized NoiseProfile'),
                  audit=dict(production_modified=False, raw_files_modified=False, physical_calibration_validated=False,
                             fitted_coefficients_experimental=True, normalization_validated=False),
                  planes=analyze(audit))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf8') as stream:
        json.dump(result, stream, ensure_ascii=False, allow_nan=False, indent=2)
    for p in result['planes']:
        print(p['folder'], p['plane'], p['status'],
              'pairs', p.get('independent_pairs', 0),
              'LOO error', round(p.get('loo_relative_mae', 0)*100, 2))
    print(args.output)


if __name__ == '__main__':
    main()
