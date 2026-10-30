"""N2b(a): camera-signal coverage and coefficient MAD, without changing denoising."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fitted(records, level):
    usable = [r for r in records if not r['levels'][level]['coherent_rejected']]
    usable.sort(key=lambda r: sum(s*s for s in r['levels'][level]['sigma_I_Co_Cg']))
    keep = usable[:max(1, math.ceil(len(usable)/4))]
    return dict(candidates=len(usable), retained_rois=[r['roi'] for r in keep],
                retained_signal=[r['signal_I_median'] for r in keep],
                sigma=[statistics.median(r['levels'][level]['sigma_I_Co_Cg'][c] for r in keep) for c in range(3)] if keep else None)


def predict_variance(nodes, signal):
    """Interpolate variance at witnessed signal levels; never extrapolate."""
    nodes = sorted(nodes, key=lambda n: n[0])
    if len(nodes)<2 or signal<nodes[0][0] or signal>nodes[-1][0]:
        return None
    for (x, a), (y, b) in zip(nodes, nodes[1:]):
        if x<=signal<=y and y>x:
            t=(signal-x)/(y-x)
            return [math.sqrt(max(0., (1-t)*p*p+t*q*q)) for p,q in zip(a,b)]
    return None


def summarize(records):
    train = sorted([r for r in records if r['training_original_5x5']], key=lambda r: r['signal_I_median'])
    held = [r for r in records if not r['training_original_5x5']]
    if len(train) != 25 or len(held) != 56:
        raise ValueError('Unexpected 9x9 split')
    # Equal-count training bands, fixed before looking at held-out patches.
    groups = [train[:8], train[8:16], train[16:]]
    boundaries = [.5*(groups[i][-1]['signal_I_median']+groups[i+1][0]['signal_I_median']) for i in range(2)]
    bands = []
    for index, group in enumerate(groups):
        target = [r for r in held if sum(r['signal_I_median'] > b for b in boundaries) == index]
        # A diagnostic flatness gate, NOT a certified noise/texture classifier.
        flat = [r for r in target if r['quadrant_signal_drift'] <= 3*r['levels'][0]['sigma_I_Co_Cg'][0]]
        bands.append(dict(training_signal_range=[group[0]['signal_I_median'], group[-1]['signal_I_median']],
                          conditional_models=[fitted(group, l) for l in range(5)],
                          held_count=len(target), flat_candidate_count=len(flat),
                          held_flat_coefficient_sigma=[
                              [statistics.median(r['levels'][l]['sigma_I_Co_Cg'][c] for r in flat if not r['levels'][l]['coherent_rejected'])
                               if any(not r['levels'][l]['coherent_rejected'] for r in flat) else None for c in range(3)]
                              for l in range(5)]))
    prediction = []
    for level in range(5):
        nodes = [(statistics.median(b['conditional_models'][level]['retained_signal']),
                  b['conditional_models'][level]['sigma']) for b in bands
                 if b['conditional_models'][level]['sigma'] is not None]
        global_model = fitted(train, level)['sigma']
        compared = []
        abstained = 0
        for r in held:
            if r['levels'][level]['coherent_rejected'] or r['quadrant_signal_drift']>3*r['levels'][0]['sigma_I_Co_Cg'][0]:
                continue
            estimate = predict_variance(nodes, r['signal_I_median'])
            if estimate is None or global_model is None:
                abstained += 1
                continue
            observed = r['levels'][level]['sigma_I_Co_Cg']
            compared.append(dict(roi=r['roi'], signal=r['signal_I_median'], observed_coefficient_sigma=observed,
                                 global_prediction=global_model, conditional_prediction=estimate))
        prediction.append(dict(nodes=nodes, compared=compared, abstained_flat_candidates=abstained,
            global_median_relative_error=[statistics.median(abs(r['global_prediction'][c]/r['observed_coefficient_sigma'][c]-1) for r in compared if r['observed_coefficient_sigma'][c]>0)
                                          if any(r['observed_coefficient_sigma'][c]>0 for r in compared) else None for c in range(3)],
            conditional_median_relative_error=[statistics.median(abs(r['conditional_prediction'][c]/r['observed_coefficient_sigma'][c]-1) for r in compared if r['observed_coefficient_sigma'][c]>0)
                                               if any(r['observed_coefficient_sigma'][c]>0 for r in compared) else None for c in range(3)]))
    return dict(training_patches=25, held_out_patches=56, boundaries=boundaries,
                global_models=[fitted(train, l) for l in range(5)], bands=bands,
                variance_interpolation=prediction,
                limits='Spatial holdout only; coefficient MAD still contains texture and is not a clean-noise truth.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--image-root', type=Path, required=True)
    p.add_argument('--profile-root', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    p.add_argument('--replay', type=Path, help='Reanalyse an immutable survey, without decoding RAW again')
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p.resolve()) for p in a.runtime_dir) + os.pathsep + env.get('PATH', '')
    names = ['src/raw/CameraDenoise.cpp', 'tests/raw_bench/camera_denoise_leakage.cpp',
             'tests/raw_bench/signal_noise_survey.py', 'tests/raw_bench/production/CMakeLists.txt']
    data = dict(schema_version=1, created_utc=stamp, study='N2b_signal_conditioned_estimation_survey',
                executable_sha256=sha256(a.tool),
                revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                sources={name: dict(sha256=sha256(root/name), text=(root/name).read_text(encoding='utf-8')) for name in names},
                scenes=[], limits=['No production filtering change, no TIFF output.',
                                   'Post-RCD signed camera coefficients, already white-balanced axes; intensity proxy not rendered Y.',
                                   'Production observer reports actual models; survey coefficient filtering is independently computed.',
                                   'Bands trained on 25 original positions, 56 other spatial positions reported separately.',
                                   'Flat-candidate gate and MAD cannot certify that weak texture is noise.',
                                   'Not a sensor-calibrated a*signal+b model, no cross-camera ISO extrapolation.'])
    failed = False
    inputs = ['base/7Y6A7599.CR3', 'base/IMG_7958.CR2', 'base/6V1A9539.CR3', 'base-hors-protocole/6V1A9613.CR3']
    if a.replay:
        previous=json.loads(a.replay.read_text(encoding='utf-8'))
        if previous.get('verdict')!='measured_not_noise_model_validation':
            raise ValueError('Cannot replay a failed survey')
        data['replay']=dict(path=str(a.replay),sha256=sha256(a.replay))
        for old in previous['scenes']:
            data['scenes'].append(dict(measurements=old['measurements'],summary=summarize(old['measurements']['patches'])))
        data['limits'].append('Replay preserves original axes and data: read provenance in the replayed source; no reinterpretation as balanced measurements.')
        inputs=[]
    for relative in inputs:
        command = [str(a.tool.resolve()), '--input', str((a.image_root/relative).resolve()),
                   '--manifest', str(root/'config/raw-normalization.json'), '--profile-root', str(a.profile_root.resolve()),
                   '--label', 'N2b-signal-survey', '--signal-survey']
        try:
            result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=180)
            record = dict(argv=command, returncode=result.returncode, stderr=result.stderr)
            if result.returncode == 0:
                record['measurements'] = json.loads(result.stdout)
                record['summary'] = summarize(record['measurements']['patches'])
            else:
                record['stdout'] = result.stdout
                failed = True
        except (subprocess.TimeoutExpired, ValueError) as exc:
            record = dict(argv=command, error=repr(exc))
            failed = True
        data['scenes'].append(record)
        print(relative, 'failed' if failed else 'measured', flush=True)
        if failed:
            break
    data['verdict'] = 'fail' if failed else 'measured_not_noise_model_validation'
    output = root/'docs/validation/raw'/f'camera-denoise-signal-survey-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, data['verdict'], flush=True)
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
