"""Development-only spatial prediction, with real signed post-RCD dark covariance.

This is a witness-certification study, not a denoiser or a sensor-noise fit.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time

# One BLAS worker in this diagnostic process; no global machine setting changed.
os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
import numpy as np
from audit_dark_mean import sha256
from capture import load_capture


OFFSETS = [(dy*lag, dx*lag) for lag in (4, 8)
           for dy, dx in ((0, 1), (1, 0), (0, -1), (-1, 0),
                          (1, 1), (1, -1), (-1, 1), (-1, -1))]
TRAIN = (16, 112)
CONTROL = (144, 240)


def samples(image, extent):
    if image.shape != (256, 256) or not np.isfinite(image).all():
        raise ValueError('Expected finite 256px scalar field')
    lo, hi = extent
    features = np.stack([image[lo+dy:hi+dy, lo+dx:hi+dx].ravel()
                         for dy, dx in OFFSETS], axis=1)
    return features, image[lo:hi, lo:hi].ravel()


def centred_cov(x, y):
    xx = x-x.mean(axis=0)
    yy = y-y.mean()
    return xx.T@xx/(len(y)-1), xx.T@yy/(len(y)-1)


def fit(observed_train, reference_train, noise_aware):
    """No truth, control pixels or control labels are accepted by this function."""
    x, y = observed_train
    nx, ny = reference_train
    if x.shape != nx.shape or y.shape != ny.shape or x.shape[0] != len(y):
        raise ValueError('Training geometry mismatch')
    if not all(np.isfinite(a).all() for a in (x, y, nx, ny)):
        raise ValueError('Nonfinite training')
    covariance, cross = centred_cov(x, y)
    noise_covariance, noise_cross = centred_cov(nx, ny)
    # Same fixed regularization in both variants. The second model removes
    # covariance with target noise, NOT the observation covariance in the denominator.
    floor = max(1e-16, float(np.trace(covariance))/len(OFFSETS)*1e-8)
    system = covariance + .01*noise_covariance + np.eye(len(OFFSETS))*floor
    weights = np.linalg.solve(system, cross-noise_cross if noise_aware else cross)
    if not np.isfinite(weights).all():
        raise ValueError('Nonfinite predictor')
    return weights, x.mean(axis=0), float(y.mean()), noise_cross


def evaluate(truth, perturbation, reference):
    observed = truth+perturbation
    train = samples(observed, TRAIN)
    control = samples(observed, CONTROL)
    prior = samples(reference, TRAIN)
    sx_train, sy_train = samples(truth, TRAIN)
    sx, sy = samples(truth, CONTROL)
    nx_train, ny_train = samples(perturbation, TRAIN)
    nx, ny = samples(perturbation, CONTROL)
    truth_variance = float(np.var(sy, ddof=1))
    noise_variance = float(np.var(ny, ddof=1))
    result = dict(truth_variance=truth_variance, noise_variance=noise_variance,
                  input_mse=float(np.mean(ny*ny)), methods={})
    for name, aware in (('ordinary', False), ('noise_aware', True)):
        w, x_mean, y_mean, noise_cross = fit(train, prior, aware)
        predicted = (control[0]-x_mean)@w+y_mean
        signal_part = (sx-sx_train.mean(axis=0))@w+sy_train.mean()
        noise_part = (nx-nx_train.mean(axis=0))@w+ny_train.mean()
        if not np.allclose(predicted, signal_part+noise_part, rtol=1e-8, atol=1e-12):
            raise ValueError('Signal/noise decomposition does not close')
        cross = float(np.dot(predicted-predicted.mean(), control[1]-control[1].mean())/(len(predicted)-1))
        # A raw correlation is not evidence of signal when RCD noise is predictable.
        corrected = cross-float(w@noise_cross)
        result['methods'][name] = dict(weights=w.tolist(),
            signal_gain=float(np.cov(signal_part, sy, ddof=1)[0, 1]/truth_variance) if truth_variance > 1e-20 else None,
            output_mse=float(np.mean((predicted-sy)**2)),
            signal_error_mse=float(np.mean((signal_part-sy)**2)),
            predicted_noise_mse=float(np.mean(noise_part**2)),
            predicted_noise_variance=float(np.var(noise_part, ddof=1)),
            signal_noise_error_cross=float(2*np.mean((signal_part-sy)*noise_part)),
            control_raw_predictable_covariance=cross,
            reference_noise_predictable_covariance=float(w@noise_cross),
            control_noise_corrected_covariance=corrected,
            corrected_covariance_over_prior_variance=corrected/max(1e-20, float(np.var(prior[1], ddof=1))))
    return result


def signal(pattern, period, amplitude):
    y, x = np.mgrid[:256, :256]
    a = 2*np.pi*x/period+.37
    b = 2*np.pi*y/period+.37
    values = dict(horizontal=np.sin(a), vertical=np.sin(b),
                  diagonal=np.sin((a+b)/np.sqrt(2)), product=np.sin(a)*np.cos(b*2/3))
    return .03 + amplitude*values[pattern]


def authenticated_selection(root, pilot_path):
    pilot = json.loads(pilot_path.read_text(encoding='utf-8'))
    if pilot['verdict'] != 'pass_signed_module_contract_not_production_or_quality':
        raise ValueError('Pilot not qualified')
    parent_path = root/pilot['freeze_path']
    if sha256(parent_path) != pilot['freeze_sha256']:
        raise ValueError('Changed pilot freeze')
    parent = json.loads(parent_path.read_text(encoding='utf-8'))
    allowed = {r['sha256'] for r in parent['selection']
               if r['split'] == 'development' and r['category'] == 'confirmed_dark'}
    selected = [p for p in pilot['pairs'] if p['label'] in
                ('confirmed_dark_ISO1600', 'confirmed_dark_ISO51200')]
    if len(selected) != 2 or any(len(p['captures']) != 2 for p in selected):
        raise ValueError('Expected two authenticated development pairs')
    for pair in selected:
        for capture in pair['captures']:
            if capture['source']['sha256'] not in allowed or sha256(capture['path']) != capture['sha256']:
                raise ValueError('Non-development capture or changed manifest')
    return selected


def load_intensity(record):
    manifest, stages = load_capture(record['path'])
    if manifest['source']['sha256'] != record['source']['sha256'] or manifest['mode'] != 'signed':
        raise ValueError('Capture provenance mismatch')
    selected = sorted((k for k in stages if k[0] == 'pre_dcp_camera_rgb'), key=lambda k:(k[1][1], k[1][0]))
    if len(selected) != 3:
        raise ValueError('Expected three previously captured fixed ROIs')
    descriptor, rgb = stages[selected[1]]
    if rgb.shape != (256, 256, 3) or not np.isfinite(rgb).all():
        raise ValueError('Invalid camera crop')
    intensity = rgb.astype(np.float64)@np.array([.25, .5, .25])
    return intensity, dict(stage=descriptor, pixels=manifest['pixels'], applied=manifest['applied'])


def summarize(rows):
    output = []
    for label in sorted({r['pair'] for r in rows}):
        group = [r for r in rows if r['pair'] == label]
        flat = [r for r in group if r['pattern'] == 'flat']
        textured = [r for r in group if r['pattern'] != 'flat']
        summary = dict(pair=label, cases=len(group), methods={})
        for name in ('ordinary', 'noise_aware'):
            null_scores = [r['measurement']['methods'][name]['corrected_covariance_over_prior_variance'] for r in flat]
            texture_scores = [r['measurement']['methods'][name]['corrected_covariance_over_prior_variance'] for r in textured]
            gains = [r['measurement']['methods'][name]['signal_gain'] for r in textured]
            summary['methods'][name] = dict(flat_corrected_scores=null_scores,
                textured_corrected_score_min=min(texture_scores),
                minimum_signal_gain=min(gains), median_signal_gain=float(np.median(gains)),
                flat_predicted_noise_variance_over_input=[r['measurement']['methods'][name]['predicted_noise_variance']/r['measurement']['noise_variance'] for r in flat],
                observed_score_gap=min(texture_scores)-max(null_scores))
        output.append(summary)
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pilot-proof', type=Path, required=True)
    p.add_argument('--matched-noise', action='store_true',
                   help='Diagnostic only: scale each dark to training sigma .002, fixed from section40')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    selection = authenticated_selection(root, args.pilot_proof)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources = ['tests/raw_bench/study_structure_prediction.py', 'tests/raw_bench/capture.py']
    freeze = dict(schema_version=1, study='structure_prediction_with_dark_noise_prior', created_utc=stamp,
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        pilot_sha256=sha256(args.pilot_proof), pairs=selection, numpy=np.__version__,
        sources={s:dict(sha256=sha256(root/s), text=(root/s).read_text(encoding='utf-8')) for s in sources},
        protocol=dict(offsets_yx=OFFSETS, train_yx_extent=TRAIN, control_yx_extent=CONTROL,
            patterns=['flat', 'horizontal', 'vertical', 'diagonal', 'product'], periods=[16, 32],
            amplitudes=[.0015, .003], phase=.37, level=.03,
            reference='Other development black, training quadrant only, both directions',
            noise_normalization='own training sigma to .002 (section40 scale)' if args.matched_noise else 'none',
            ridge_noise_covariance=.01, numerical_relative_diagonal_floor=1e-8,
            recipe='Cov(X,X)+.01Cov(NX,NX); RHS Cov(X,Y), or Cov(X,Y)-Cov(NX,NY)',
            decision='Report score overlap and signal gain; no fitted detection threshold, no new filter.'),
        limits=['Four development frames, one camera, read noise only, no intact validation.',
                'Different train/control quadrants have no shared pixels but long-range/fixed-pattern dependence may remain.',
                'Real post-RCD black residual added to synthetic scalar signal does not recreate signal-dependent CFA/RCD noise.',
                'External dark covariance is privileged and only valid here at fixed explicit pilot levels/gains.',
                'Common fixed pattern may predict noise and may not be separated by a pair difference.',
                'No rendered image or actual denoiser changed; no physical a*signal+b model, DCP, latency or RAM qualification.'])
    study = 'structure-prediction-matched' if args.matched_noise else 'structure-prediction'
    if args.matched_noise:
        freeze['limits'].append('Matched-noise control rescales real residual amplitude; not a physical ISO model or real sensor quality score.')
    frozen = root/'docs/validation/raw'/f'{study}-freeze-v001-{stamp}.json'
    with frozen.open('x', encoding='utf-8') as handle:
        json.dump(freeze, handle, ensure_ascii=False, indent=2, allow_nan=False)
    start = time.monotonic()
    result = dict(schema_version=1, created_utc=stamp, freeze_path=str(frozen.relative_to(root)),
                  freeze_sha256=sha256(frozen), rows=[], capture_metadata=[])
    try:
        for pair in selection:
            loaded = [load_intensity(c) for c in pair['captures']]
            result['capture_metadata'].append(dict(pair=pair['label'], contexts=[v[1] for v in loaded]))
            if loaded[0][1]['applied'] != loaded[1][1]['applied']:
                raise ValueError('Pilot contexts differ')
            for shot in range(2):
                perturbation, reference = loaded[shot][0], loaded[1-shot][0]
                scales = [1., 1.]
                if args.matched_noise:
                    scales = [.002/float(np.std(samples(field, TRAIN)[1], ddof=1))
                              for field in (perturbation, reference)]
                    if not all(np.isfinite(v) and v > 0 for v in scales):
                        raise ValueError('Invalid matched noise normalization')
                    perturbation, reference = perturbation*scales[0], reference*scales[1]
                cases = [('flat', 0, 0)] + [(pattern, period, amplitude)
                    for pattern in ('horizontal', 'vertical', 'diagonal', 'product')
                    for period in (16, 32) for amplitude in (.0015, .003)]
                for pattern, period, amplitude in cases:
                    truth = np.full((256, 256), .03) if pattern == 'flat' else signal(pattern, period, amplitude)
                    result['rows'].append(dict(pair=pair['label'], shot=shot, pattern=pattern,
                        period=period, amplitude=amplitude, noise_scales=scales,
                        measurement=evaluate(truth, perturbation, reference)))
            print(pair['label'], 'spatial prediction measured', flush=True)
        if len(result['rows']) != 68:
            raise ValueError('Missing cases')
        result['summary'] = summarize(result['rows'])
        result['verdict'] = 'measured_prediction_diagnostic_not_denoiser_or_certified_witness'
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
        result['verdict'] = 'fail'
    result['seconds'] = time.monotonic()-start
    output = root/'docs/validation/raw'/f'{study}-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, result['verdict'], result.get('error', ''), flush=True)
    if 'summary' in result:
        print(json.dumps(result['summary']), flush=True)
    return int(result['verdict'] == 'fail')


if __name__ == '__main__':
    raise SystemExit(main())
