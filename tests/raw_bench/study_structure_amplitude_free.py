"""Stress fixed spatial prediction without external noise amplitude; no replacement filter."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
import cv2
import numpy as np
from audit_dark_mean import sha256
from study_structure_prediction import (TRAIN, CONTROL, OFFSETS, samples, centred_cov,
                                        authenticated_selection, load_intensity, signal)


CASES = [('flat', 0), ('sinusoid', .0015), ('irregular_fine', .0015),
         ('irregular_coarse', .0015), ('novel_edge', .003), ('novel_line', .003),
         ('novel_spot', .003), ('seen_edge', .003)]


def fit_observation(train):
    """Only observed training pixels: no covariance prior, truth or control."""
    x, y = train
    if x.ndim != 2 or x.shape != (len(y), len(OFFSETS)) or not np.isfinite(x).all() or not np.isfinite(y).all():
        raise ValueError('Invalid training')
    covariance, cross = centred_cov(x, y)
    ridge = max(1e-20, 1e-4*float(np.trace(covariance))/len(OFFSETS))
    weights = np.linalg.solve(covariance+np.eye(len(OFFSETS))*ridge, cross)
    if not np.isfinite(weights).all():
        raise ValueError('Nonfinite fit')
    return weights, x.mean(axis=0), float(y.mean())


def shape(reference):
    x, y = samples(reference, TRAIN)
    variance = float(np.var(y, ddof=1))
    if not np.isfinite(variance) or variance <= 1e-24:
        raise ValueError('Reference shape unavailable')
    covariance, cross = centred_cov(x, y)
    return covariance/variance, cross/variance


def estimate(residual_variance, weights, prior):
    covariance, cross = prior
    denominator = float(1+weights@covariance@weights-2*weights@cross)
    return (float(residual_variance/denominator) if denominator > 1e-12 else None), denominator


def smooth(image, sigma):
    radius = int(np.ceil(3*sigma))
    positions = np.arange(-radius, radius+1, dtype=float)
    kernel = np.exp(-.5*(positions/sigma)**2)
    kernel /= kernel.sum()
    return cv2.sepFilter2D(image, -1, kernel, kernel, borderType=cv2.BORDER_REFLECT_101)


def template(name, amplitude):
    y, x = np.mgrid[:256, :256]
    if name == 'flat':
        values = np.zeros((256,256))
    elif name == 'sinusoid':
        return signal('product', 16, amplitude)
    elif name.startswith('irregular_'):
        rng = np.random.default_rng(74042 if name == 'irregular_fine' else 74043)
        values = smooth(rng.normal(size=(256,256)), .8 if name == 'irregular_fine' else 3.)
        values = (values-values.mean())/values.std(ddof=1)
    elif name == 'novel_edge':
        values = (x >= 192).astype(float)
    elif name == 'seen_edge':
        values = ((x % 128) >= 64).astype(float)
    elif name == 'novel_line':
        values = ((x >= 191) & (x < 193) & (y >= 170) & (y < 214)).astype(float)
    elif name == 'novel_spot':
        values = np.exp(-.5*((x-192)**2+(y-192)**2)/4.)
    else:
        raise ValueError('Unknown template')
    return .03+amplitude*values


def planes(field):
    output = [('pixel_I', field)]
    current = field
    for level in range(3):
        step = 1 << level
        kernel = np.zeros(4*step+1)
        kernel[::step] = np.array([1,4,6,4,1])/16
        base = cv2.sepFilter2D(current, -1, kernel, kernel, borderType=cv2.BORDER_REFLECT_101)
        output.append((f'B3_{level}', current-base))
        current = base
    return output


def qualify(observed, truth, priors):
    train = samples(observed, TRAIN)
    x, y = samples(observed, CONTROL)
    weights, mu_x, mu_y = fit_observation(train)
    prediction = (x-mu_x)@weights+mu_y
    sx_train, sy_train = samples(truth, TRAIN)
    sx, sy = samples(truth, CONTROL)
    signal_prediction = (sx-sx_train.mean(axis=0))@weights+sy_train.mean()
    true_noise_variance = float(np.var(y-sy, ddof=1))
    signal_variance = float(np.var(sy, ddof=1))
    residual_variance = float(np.var(y-prediction, ddof=1))
    result = dict(weights=weights.tolist(), true_noise_variance=true_noise_variance,
        signal_variance=signal_variance, residual_variance=residual_variance,
        signal_prediction_gain=float(np.cov(signal_prediction, sy)[0,1]/signal_variance) if signal_variance > 1e-20 else None,
        predictions={})
    for name, prior in priors.items():
        variance, denominator = estimate(residual_variance, weights, prior)
        result['predictions'][name] = dict(denominator=denominator, variance=variance,
            sigma_ratio=float(np.sqrt(variance/true_noise_variance)) if variance is not None and variance >= 0 and true_noise_variance > 0 else None)
    return result


def native(tool, observed, truth, env):
    payload = {}
    hashes = {}
    for name, field in (('observed', observed), ('truth', truth)):
        pixels = np.repeat(field[...,None], 3, axis=2).astype('<f4').tobytes()
        payload[name] = base64.b64encode(pixels).decode('ascii')
        hashes[name] = hashlib.sha256(pixels).hexdigest()
    run = subprocess.run([str(tool.resolve())], input=json.dumps(payload)+'\n',
        env=env, capture_output=True, text=True, encoding='utf-8', timeout=40)
    if run.returncode:
        raise RuntimeError(f'Current filter probe failed: {run.stderr}')
    result = json.loads(run.stdout)
    result['input_sha256'] = hashes
    return result


def summarize(rows):
    output = []
    keys = sorted({(r['noise_mode'], r['pair'], r['pattern']) for r in rows})
    for mode, pair, pattern in keys:
        group = [r for r in rows if (r['noise_mode'], r['pair'], r['pattern']) == (mode, pair, pattern)]
        for plane in ('pixel_I', 'B3_0', 'B3_1', 'B3_2'):
            selected = [r['planes'][plane] for r in group]
            item = dict(mode=mode, pair=pair, pattern=pattern, plane=plane, priors={})
            for prior in ('matched_shape', 'rotated_shape', 'other_iso_shape', 'white_shape'):
                values = [r['predictions'][prior]['sigma_ratio'] for r in selected if r['predictions'][prior]['sigma_ratio'] is not None]
                item['priors'][prior] = dict(invalid_cases=len(selected)-len(values),
                    min_median_max=[float(min(values)), float(np.median(values)), float(max(values))] if values else None)
            output.append(item)
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pilot-proof', type=Path, required=True)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    args = p.parse_args()
    cv2.setNumThreads(1)
    root = Path(__file__).resolve().parents[2]
    selection = authenticated_selection(root, args.pilot_proof)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    source_names = ['tests/raw_bench/study_structure_amplitude_free.py', 'tests/raw_bench/structure_baseline_probe.cpp',
        'tests/raw_bench/study_structure_prediction.py', 'src/raw/CameraDenoise.cpp', 'src/raw/CameraDenoise.h']
    freeze = dict(schema_version=1, created_utc=stamp, study='amplitude_free_structure_stress',
        revision=subprocess.check_output(['git','rev-parse','HEAD'], cwd=root, text=True).strip(),
        pilot_sha256=sha256(args.pilot_proof), pairs=selection, tool_sha256=sha256(args.tool),
        numpy=np.__version__, opencv=cv2.__version__,
        sources={s:dict(sha256=sha256(root/s), text=(root/s).read_text(encoding='utf-8')) for s in source_names},
        protocol=dict(cases=CASES, offsets=OFFSETS, train=TRAIN, control=CONTROL,
            fit='Observed covariance ONLY, ridge 1e-4*trace(CovXX)/16; numerical floor1e-20',
            planes=['pixel_I','B3_0','B3_1','B3_2'], noise_modes=['pilot_amplitude','matched_train_sigma_.002'],
            priors=['other sameISO training shape','rot90 same prior','otherISO same shot training shape','white'],
            stress='Novel features absent from training; seen edge crosses both regions; irregular seeds74042/74043, Gaussian sigma.8/3.',
            decision='No threshold or ridge retuning. Compare to actual filter100/100 and pooled sigma summaries; do not inject candidate into filter.'),
        limits=['Four development black frames only; additive post-RCD scalar read-noise replay, not photons/CFA signal dependence.',
            'Shape of external noise still required; no amplitude prior in fit or residual formula.',
            'Pooled production sigmas are observer summaries, not exact per-pixel routed conditional models.',
            'B3 Python f64 diagnostic has same kernel/support as current module, not claimed bit-exact with C++ f32.',
            'Current filter is run on achromatic camera RGB; this does not qualify chroma or DCP.',
            'Prediction is not an image reconstruction and good noise sigma is not proof of detail preservation.'])
    frozen = root/'docs/validation/raw'/f'structure-amplitude-free-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    result = dict(schema_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),
        freeze_sha256=sha256(frozen), rows=[])
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    start = time.monotonic()
    try:
        fields = {pair['label']:[load_intensity(c)[0] for c in pair['captures']] for pair in selection}
        for mode in ('pilot_amplitude','matched_train_sigma_.002'):
            for pair, pair_fields in fields.items():
                alternate = next(key for key in fields if key != pair)
                for shot in range(2):
                    perturbation = pair_fields[shot]
                    scale = .002/float(np.std(samples(perturbation,TRAIN)[1],ddof=1)) if mode != 'pilot_amplitude' else 1.
                    perturbation = perturbation*scale
                    references = dict(matched_shape=pair_fields[1-shot], rotated_shape=np.rot90(pair_fields[1-shot]),
                                      other_iso_shape=fields[alternate][shot])
                    shapes = {name:{label:shape(field) for label,field in planes(reference)} for name,reference in references.items()}
                    for pattern, amplitude in CASES:
                        truth = template(pattern,amplitude).astype(np.float32).astype(float)
                        observed = (truth+perturbation).astype(np.float32).astype(float)
                        clean_planes = dict(planes(truth))
                        metrics = {}
                        for label, field in planes(observed):
                            priors = {name:levels[label] for name,levels in shapes.items()}
                            priors['white_shape'] = (np.eye(len(OFFSETS)),np.zeros(len(OFFSETS)))
                            metrics[label] = qualify(field,clean_planes[label],priors)
                        baseline = native(args.tool,observed,truth,env)
                        for level in range(3):
                            sigma = baseline['noisy_100']['models'][level]['pooled_sigma_I']
                            target_variance = metrics[f'B3_{level}']['true_noise_variance']
                            metrics[f'B3_{level}']['current_pooled_sigma_ratio'] = float(sigma/np.sqrt(target_variance)) if target_variance > 0 else None
                        result['rows'].append(dict(noise_mode=mode,pair=pair,shot=shot,pattern=pattern,amplitude=amplitude,
                            source_noise_scale=scale, planes=metrics,current_filter=baseline))
                print(mode,pair,'measured',flush=True)
        if len(result['rows']) != 64:
            raise ValueError('Missing fixed stress cases')
        result['summary'] = summarize(result['rows'])
        result['verdict'] = 'measured_stress_only_no_candidate_filter_or_witness_certification'
    except Exception as exc:
        result['verdict'] = 'fail'
        result['error'] = f'{type(exc).__name__}: {exc}'
    result['seconds'] = time.monotonic()-start
    output = root/'docs/validation/raw'/f'structure-amplitude-free-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__ == '__main__':
    raise SystemExit(main())
