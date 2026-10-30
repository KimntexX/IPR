"""Replay frozen predictor weights: audit residual-variance compensation, without refitting."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
import numpy as np
from audit_dark_mean import sha256
from study_structure_prediction import (TRAIN, CONTROL, authenticated_selection, load_intensity,
                                        samples, signal, centred_cov)


def compensated_variance(residual_variance, weights, covariance, cross):
    return float(residual_variance-weights@covariance@weights+2*weights@cross)


def shape_only_variance(residual_variance, weights, covariance, cross, reference_variance):
    if reference_variance <= 0:
        return None
    amplification = 1+(weights@covariance@weights-2*weights@cross)/reference_variance
    return float(residual_variance/amplification) if amplification > 1e-12 else None


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prediction-proof', type=Path, action='append', required=True)
    p.add_argument('--pilot-proof', type=Path, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    parents = [json.loads(path.read_text(encoding='utf-8')) for path in args.prediction_proof]
    for parent in parents:
        if parent['verdict'] != 'measured_prediction_diagnostic_not_denoiser_or_certified_witness' or len(parent['rows']) != 68:
            raise ValueError('Incomplete predictor parent')
        if sha256(root/parent['freeze_path']) != parent['freeze_sha256']:
            raise ValueError('Changed predictor freeze')
        frozen = json.loads((root/parent['freeze_path']).read_text(encoding='utf-8'))
        if frozen['sources']['tests/raw_bench/study_structure_prediction.py']['sha256'] != sha256(root/'tests/raw_bench/study_structure_prediction.py'):
            # v001 before --matched-noise has exactly the same fit/evaluate routines,
            # but a different CLI. Verify the fitted weights by decomposition below;
            # preserve the parent text rather than claiming a whole-source match.
            parent['audit_import_scope'] = 'CLI differs; frozen weights used directly, no fit invoked'
    selection = authenticated_selection(root, args.pilot_proof)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    source_names = ['tests/raw_bench/audit_structure_residual.py', 'tests/raw_bench/study_structure_prediction.py']
    freeze = dict(schema_version=2, created_utc=stamp,
        supersedes='structure-residual-v001-20261030T134436Z.json',
        parents=[dict(path=str(path), sha256=sha256(path)) for path in args.prediction_proof],
        pilot_sha256=sha256(args.pilot_proof),
        sources={s:dict(sha256=sha256(root/s), text=(root/s).read_text(encoding='utf-8')) for s in source_names},
        protocol='Reuse stored weights. Subtractive variance compensation, plus var(Y-wX)/(1+w*Cxx*w-2*w*cxy) using unit-target-variance correlation shape from OTHER training dark.',
        limits=['Variance, not DC correction. Negative estimates retained, never silently clamped.',
                'External prior at explicit pilot calibration, read-noise additive replay, not operational single-image estimator.',
                'Shape-only residual formula does not use prior amplitude, but stored predictor training still used amplitude in ridge/prior; not an entirely prior-amplitude-independent pipeline.',
                'Residual still contains unpredicted structure and signal/noise cross terms; no universal unbiasedness claim.',
                'Oracle noise covariance shown separately for attribution, never used to train or qualify an operational model.'])
    frozen_path = root/'docs/validation/raw'/f'structure-residual-freeze-v002-{stamp}.json'
    with frozen_path.open('x', encoding='utf-8') as handle:
        json.dump(freeze, handle, ensure_ascii=False, indent=2, allow_nan=False)
    result = dict(schema_version=2, created_utc=stamp, freeze_path=str(frozen_path.relative_to(root)),
                  freeze_sha256=sha256(frozen_path), rows=[], summary=[])
    try:
        fields = {pair['label']:[load_intensity(c)[0] for c in pair['captures']] for pair in selection}
        for parent_index, parent in enumerate(parents):
            for row in parent['rows']:
                noise = fields[row['pair']][row['shot']]*row.get('noise_scales', [1, 1])[0]
                reference = fields[row['pair']][1-row['shot']]*row.get('noise_scales', [1, 1])[1]
                truth = np.full((256, 256), .03) if row['pattern'] == 'flat' else signal(row['pattern'], row['period'], row['amplitude'])
                observed = truth+noise
                train_x, train_y = samples(observed, TRAIN)
                x, y = samples(observed, CONTROL)
                _, sy = samples(truth, CONTROL)
                nxx, nxy = centred_cov(*samples(reference, TRAIN))
                oracle_xx, oracle_xy = centred_cov(*samples(noise, CONTROL))
                true_variance = float(np.var(samples(noise, CONTROL)[1], ddof=1))
                for name, measured in row['measurement']['methods'].items():
                    weights = np.array(measured['weights'])
                    predicted = (x-train_x.mean(axis=0))@weights+train_y.mean()
                    replay_mse = float(np.mean((predicted-sy)**2))
                    if not np.isclose(replay_mse, measured['output_mse'], rtol=1e-9, atol=1e-16):
                        raise ValueError('Stored predictor replay differs')
                    residual_variance = float(np.var(y-predicted, ddof=1))
                    estimated = compensated_variance(residual_variance, weights, nxx, nxy)
                    oracle = compensated_variance(residual_variance, weights, oracle_xx, oracle_xy)
                    shape_estimate = shape_only_variance(residual_variance, weights, nxx, nxy,
                                                         float(np.var(samples(reference, TRAIN)[1], ddof=1)))
                    result['rows'].append(dict(parent_index=parent_index, pair=row['pair'], shot=row['shot'],
                        pattern=row['pattern'], period=row['period'], amplitude=row['amplitude'], method=name,
                        true_noise_variance=true_variance, uncorrected_residual_variance=residual_variance,
                        compensated_variance=estimated, oracle_compensated_variance=oracle,
                        shape_only_variance=shape_estimate,
                        shape_only_sigma_over_truth=float(np.sqrt(shape_estimate/true_variance)) if shape_estimate is not None and shape_estimate >= 0 else None,
                        observed_sigma_over_truth=float(np.std(y, ddof=1)/np.sqrt(true_variance)),
                        compensated_sigma_over_truth=float(np.sqrt(estimated/true_variance)) if estimated >= 0 else None,
                        replay_mse=replay_mse))
        for parent_index in range(len(parents)):
            for pair in fields:
                for method in ('ordinary', 'noise_aware'):
                    group = [r for r in result['rows'] if r['parent_index'] == parent_index and r['pair'] == pair and r['method'] == method]
                    values = [r['compensated_sigma_over_truth'] for r in group if r['compensated_sigma_over_truth'] is not None]
                    shape_values = [r['shape_only_sigma_over_truth'] for r in group if r['shape_only_sigma_over_truth'] is not None]
                    result['summary'].append(dict(parent_index=parent_index, pair=pair, method=method, cases=len(group),
                        negative_variance_cases=sum(r['compensated_variance'] < 0 for r in group),
                        invalid_shape_cases=sum(r['shape_only_sigma_over_truth'] is None for r in group),
                        shape_only_sigma_ratio_min_median_max=[float(min(shape_values)), float(np.median(shape_values)), float(max(shape_values))] if shape_values else None,
                        compensated_sigma_ratio_min_median_max=[float(min(values)), float(np.median(values)), float(max(values))],
                        observed_sigma_ratio_min_median_max=[float(f([r['observed_sigma_over_truth'] for r in group])) for f in (min, np.median, max)]))
        result['verdict'] = 'measured_residual_audit_no_refit_or_production_estimator'
    except Exception as exc:
        result['verdict'] = 'fail'
        result['error'] = f'{type(exc).__name__}: {exc}'
    output = root/'docs/validation/raw'/f'structure-residual-v002-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, result['verdict'], result.get('error', ''), flush=True)
    print(json.dumps(result['summary']), flush=True)
    return int(result['verdict'] == 'fail')


if __name__ == '__main__':
    raise SystemExit(main())
