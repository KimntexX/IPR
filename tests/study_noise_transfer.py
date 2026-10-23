"""Compare effective noise models on immutable pair statistics; no RAW/production writes."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import numpy as np
import scipy
from scipy.optimize import least_squares, nnls


MODELS = ('affine', 'dark_fixed', 'relative_affine', 'robust_relative', 'quadratic')


def fit_model(points, kind, dark):
    if kind not in MODELS:
        raise ValueError('Unknown model')
    x = np.array([p['signal_dn'] for p in points], dtype=float)
    y = np.array([p['variance_dn2'] for p in points], dtype=float)
    if len(x) < 3 or not np.isfinite(x).all() or not np.isfinite(y).all() or np.ptp(x) < 1e-8:
        raise ValueError('Insufficient finite signal range')
    if np.any(x <= 0) or np.any(y <= 0) or not np.isfinite(dark) or dark < 0:
        raise ValueError('Invalid signal or variance')
    counts = Counter(p['pair_id'] for p in points)
    weights = np.array([1 / counts[p['pair_id']] for p in points])
    scale = float(x.max())
    z = x / scale
    design = np.column_stack((z, np.ones_like(z)))
    target = y.copy()
    if kind == 'quadratic':
        design = np.column_stack((z*z, z, np.ones_like(z)))
    elif kind == 'dark_fixed':
        design, target = z[:, None], y - dark
    if kind in ('relative_affine', 'robust_relative'):
        weights = weights / (y*y)
    matrix, target = design * np.sqrt(weights[:, None]), target * np.sqrt(weights)
    parameters = nnls(matrix, target)[0]
    if kind == 'robust_relative':
        # 5% relative-residual scale, experimental and held fixed for every fold.
        result = least_squares(lambda v: matrix @ v - target, parameters,
                               bounds=(0, np.inf), loss='soft_l1', f_scale=.05)
        if not result.success:
            raise ValueError('Optimizer failed: ' + result.message)
        parameters = result.x
    if kind == 'quadratic':
        c, a, b = parameters[0] / scale**2, parameters[1] / scale, parameters[2]
    elif kind == 'dark_fixed':
        c, a, b = 0., parameters[0] / scale, dark
    else:
        c, a, b = 0., parameters[0] / scale, parameters[1]
    return dict(c=float(c), a=float(a), b=float(b), signal_range_dn=[float(x.min()), float(x.max())])


def predict(model, x):
    return model['c'] * x*x + model['a'] * x + model['b']


def validate(points, kind, dark, grouping):
    folds = []
    for group in sorted(set(p[grouping] for p in points)):
        train = [p for p in points if p[grouping] != group]
        held = [p for p in points if p[grouping] == group]
        if len(set(p['pair_id'] for p in train)) < 3:
            folds.append(dict(held=group, status='insufficient_training_pairs'))
            continue
        try:
            model = fit_model(train, kind, dark)
        except ValueError as error:
            folds.append(dict(held=group, status='unfitted', reason=str(error)))
            continue
        estimates = [dict(pair_id=p['pair_id'], roi=p['roi'], signal_dn=p['signal_dn'],
                          observed_dn2=p['variance_dn2'], predicted_dn2=predict(model, p['signal_dn']),
                          relative_error=(predict(model, p['signal_dn']) / p['variance_dn2'] - 1),
                          extrapolated=not model['signal_range_dn'][0] <= p['signal_dn'] <= model['signal_range_dn'][1])
                     for p in held]
        # Pair-balanced aggregation even for dates/exposure groups with unequal ROI counts.
        ids = sorted(set(p['pair_id'] for p in held))
        errors = [float(np.mean([abs(e['relative_error']) for e in estimates if e['pair_id'] == pid])) for pid in ids]
        folds.append(dict(held=group, status='measured', model=model, predictions=estimates,
                          pair_mean_absolute_relative_errors=errors))
    errors = [e for f in folds if f['status'] == 'measured' for e in f['pair_mean_absolute_relative_errors']]
    return dict(folds=folds, evaluated_pairs=len(errors),
                relative_mae=float(np.mean(errors)) if errors else None,
                maximum_pair_relative_mae=max(errors) if errors else None)


def enrich(record, frames, measurements):
    points = []
    for original in record['points']:
        p = dict(original)
        left, right = p['pair_id'].split(' | ')
        a, b = frames[left], frames[right]
        measurement = measurements[(left, right, tuple(record['offset']), p['roi'])]
        p.update(capture_day=a['metadata']['datetime'][:10],
                 same_capture_day=a['metadata']['datetime'][:10] == b['metadata']['datetime'][:10],
                 exposure_group=str(a['metadata']['exposure_s']),
                 raw_relative_drift=measurement['relative_drift'],
                 useful_signal_relative_drift=abs(measurement['mean_difference_dn']) / p['signal_dn'],
                 structure_ratio=measurement['difference_block_structure_ratio'],
                 horizontal_lag1=measurement['difference_horizontal_lag1'])
        if not p['same_capture_day']:
            raise ValueError('Pair spans different dates')
        points.append(p)
    return points


def analyze(ptc, audit):
    frames = {f['path']: f for g in audit['groups'] for f in g['frames']}
    measurements = {(p['left'], p['right'], tuple(m['offset']), m['roi']): m
                    for g in audit['groups'] for p in g['pair_candidates'] for m in p['measurements']}
    records = []
    for source in ptc['planes']:
        if source['status'] != 'exploratory_only':
            continue
        points = enrich(source, frames, measurements)
        dark = source['dark_variance_dn2']
        record = dict(folder=source['folder'], plane=source['plane'], points=points,
                      dark_variance_dn2=dark, models={}, sensitivity=[])
        record['correlation_diagnostics'] = dict(
            dark_horizontal_lag1_median=float(np.median([
                m['difference_horizontal_lag1'] for m in source['dark_pair_measurements']])),
            light_horizontal_lag1_median=float(np.median([p['horizontal_lag1'] for p in points])),
            note='Adjacent samples of same CFA plane, two mosaic columns apart; not a full covariance model')
        for kind in MODELS:
            model = fit_model(points, kind, dark)
            record['models'][kind] = dict(fitted=model, intercept_to_dark_ratio=model['b'] / dark if dark > 0 else None,
                validation={key: validate(points, kind, dark, key)
                            for key in ('pair_id', 'exposure_group', 'capture_day')})
        for field, threshold in (('raw_relative_drift', .005), ('raw_relative_drift', .0025),
                                 ('useful_signal_relative_drift', .01)):
            subset = [p for p in points if p[field] <= threshold]
            item = dict(field=field, maximum=threshold, points=len(subset),
                        independent_pairs=len(set(p['pair_id'] for p in subset)))
            if item['independent_pairs'] >= 4:
                item.update(status='measured', fitted=fit_model(subset, 'relative_affine', dark),
                            validation=validate(subset, 'relative_affine', dark, 'pair_id'))
            else:
                item['status'] = 'insufficient_pairs'
            record['sensitivity'].append(item)
        record['days'] = []
        for day in sorted(set(p['capture_day'] for p in points)):
            subset = [p for p in points if p['capture_day'] == day]
            item = dict(day=day, independent_pairs=len(set(p['pair_id'] for p in subset)),
                        signal_range_dn=[min(p['signal_dn'] for p in subset), max(p['signal_dn'] for p in subset)])
            if item['independent_pairs'] >= 3:
                item['relative_affine'] = fit_model(subset, 'relative_affine', dark)
            record['days'].append(item)
        records.append(record)
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ptc', type=Path, required=True)
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path)
    args = parser.parse_args()
    inputs = {key: path.read_bytes() for key, path in (('ptc', args.ptc), ('audit', args.audit))}
    ptc, audit = json.loads(inputs['ptc']), json.loads(inputs['audit'])
    if ptc['audit_sha256'] != hashlib.sha256(inputs['audit']).hexdigest():
        parser.error('PTC and audit provenance mismatch')
    now = datetime.now(timezone.utc)
    previous = json.loads(args.previous.read_bytes()) if args.previous else None
    data = dict(campaign_id='noise-transfer-v002-' + now.strftime('%Y%m%dT%H%M%SZ'), schema_version=1,
        data_version=2, measured_at_utc=now.isoformat(),
        supersedes_campaign_id=previous['campaign_id'] if previous else None,
        revision_reason='Add plane-wise dark/light lag-one diagnostics; model and holdout definitions unchanged',
        inputs={key: dict(path=str(path), sha256=hashlib.sha256(inputs[key]).hexdigest())
                for key, path in (('ptc', args.ptc), ('audit', args.audit))},
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
        configuration=dict(models=list(MODELS), units='decoder DN and DN^2',
            weighting='equal total weight per pair; relative models additionally divide residual by observed variance',
            robust_loss='soft_l1; relative residual scale 0.05',
            validation='leave pair/exposure duration/capture day out; dark reference always from Oct22, not held out',
            limitations=['capture date is not proof of session or lighting identity',
                         'no independently held-out dark pair; not a physical sensor calibration',
                         'same duration across days grouped together; distinct durations may be close in signal',
                         'model comparison exploratory, no independent final test set',
                         'no new pixel measurements; covariance and robust pixel estimators require RAW re-read']),
        audit=dict(production_modified=False, raw_files_modified=False, coefficients_validated=False),
        planes=analyze(ptc, audit))
    with args.output.open('x', encoding='utf8') as stream:
        json.dump(data, stream, ensure_ascii=False, allow_nan=False, indent=2)
    print(args.output)
    for iso in sorted(set(r['folder'] for r in data['planes']), key=lambda s: int(s.split('-')[1])):
        print(iso)
        for kind in MODELS:
            selected = [r['models'][kind]['validation'] for r in data['planes'] if r['folder'] == iso]
            print(kind, {key: [round(100*s[key]['relative_mae'], 2) if s[key]['relative_mae'] is not None else None
                              for s in selected] for key in ('pair_id', 'exposure_group', 'capture_day')})


if __name__ == '__main__':
    main()
