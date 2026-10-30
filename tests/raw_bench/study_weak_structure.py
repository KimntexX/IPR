"""Freeze an estimator/structure diagnostic, not a replacement denoiser or a quality qualification."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
from audit_dark_mean import sha256


def summarize(data):
    rows = data['cases']
    if len(rows) != 32:
        raise ValueError('Expected all 32 diagnostic cases')
    result = []
    for pattern in ('horizontal', 'vertical', 'diagonal', 'product'):
        group = [r for r in rows if r['pattern'] == pattern]
        gains = [r['clean_100']['pattern_gain'] for r in group]
        contaminated = sum(any(s['pooled_sigma_I'] > 1e-7 for s in r['clean_100']['models']) for r in group)
        result.append(dict(pattern=pattern, cases=len(group), minimum_clean_gain=min(gains),
            maximum_clean_gain=max(gains), clean_cases_with_nonzero_noise_estimate=contaminated,
            noisy_mse_ratios=[r['noisy_100']['rgb_mse']/r['noisy_zero']['rgb_mse'] for r in group]))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    p.add_argument('--layout-control', action='store_true', help='Clean local signal held fixed, flat global surroundings added')
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources = ['tests/raw_bench/weak_structure_probe.cpp', 'tests/raw_bench/study_weak_structure.py',
               'src/raw/CameraDenoise.cpp', 'src/raw/CameraDenoise.h', 'tests/raw_bench/CMakeLists.txt']
    freeze = dict(schema_version=1, created_utc=stamp, study='weak_structure_estimator_diagnostic',
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        tool_sha256=sha256(args.tool),
        sources={name:dict(sha256=sha256(root/name), text=(root/name).read_text(encoding='utf-8')) for name in sources},
        protocol=dict(patterns=['horizontal', 'vertical', 'diagonal', 'product'], periods=[16, 32],
            phases=[0, .73], amplitudes=[.0015, .003], level=.03, extent=256, roi=[64, 64, 128, 128],
            master_clean=[0, 25, 100], master_noisy=[0, 100], noise_seed=19012026,
            noise='Same achromatic binomial3 Gaussian realization, normalized sample sigma .002',
            question='Does the current estimator assign noise power to known noiseless structure? Does orientation change retention?',
            action='Diagnostic only. No parameter selection, no candidate threshold optimized from these results.'),
        historical_overlap='patch_noise sections249-250/273-274 already tested low-rank/covariance variants; not repeated here.',
        limits=['Synthetic correlated noise is not a physical sensor/RCD covariance.',
                'One shared realization, no independent validation or frequency search to choose a winning threshold.',
                'Observer can reveal contaminated estimation, but cannot isolate all filtering effects causally.',
                'Current source kept fixed; no denoiser change, no RAW holdout decoded, no DCP or photos qualified.'])
    study = 'weak-structure-context' if args.layout_control else 'weak-structure-estimator'
    if args.layout_control:
        freeze['protocol'] = dict(patterns=['horizontal', 'vertical', 'diagonal', 'product'], period=16,
            phase=0, amplitude=.0015, level=.03, input_sizes=[256, 768], roi_size=96,
            padding=256, unchanged_local_margin=80, maximum_filter_margin=65,
            noise='None: deliberately noiseless structural control', master=100,
            question='Does adding distant clean flat source change retention of exactly the same local motif?',
            action='Diagnostic only; global witnesses, tile layout and global DC balance can all change.')
    frozen = root/'docs/validation/raw'/f'{study}-freeze-v001-{stamp}.json'
    with frozen.open('x', encoding='utf-8') as handle:
        json.dump(freeze, handle, ensure_ascii=False, indent=2, allow_nan=False)
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH', '')
    result = dict(schema_version=1, created_utc=stamp, freeze_path=str(frozen.relative_to(root)),
                  freeze_sha256=sha256(frozen))
    try:
        command = [str(args.tool.resolve())] + (['--layout-control'] if args.layout_control else [])
        run = subprocess.run(command, cwd=root, env=env, capture_output=True,
                             text=True, encoding='utf-8', errors='replace', timeout=180)
        result['command'] = dict(returncode=run.returncode, stderr=run.stderr)
        if run.returncode:
            raise RuntimeError('Native structure diagnostic failed')
        result['measurements'] = json.loads(run.stdout)
        if args.layout_control:
            if len(result['measurements']['layouts']) != 4:
                raise ValueError('Missing layout control')
            result['summary'] = [dict(pattern=r['pattern'],
                original_gain=r['original_clean_100']['pattern_gain'],
                surrounded_gain=r['flat_surround_clean_100']['pattern_gain']) for r in result['measurements']['layouts']]
        else:
            result['summary'] = summarize(result['measurements'])
        result['verdict'] = 'measured_diagnostic_not_new_filter_or_production_validation'
    except Exception as exc:
        result['error'] = f'{type(exc).__name__}: {exc}'
        result['verdict'] = 'fail'
    output = root/'docs/validation/raw'/f'{study}-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(result, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, result['verdict'], result.get('error', ''), flush=True)
    if 'summary' in result:
        print(json.dumps(result['summary']), flush=True)
    return int(result['verdict'] == 'fail')


if __name__ == '__main__':
    raise SystemExit(main())
