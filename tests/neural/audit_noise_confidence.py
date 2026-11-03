"""Measure correction attenuation on known procedural content; no model fitting."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch

from bayer_canonical import canonicalize
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import edge_confidence
from memory_budget import MemoryGuard
from run_diverse_moment_pilot import edge_image
from run_noisy_moment_pilot import observation
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def run(root, plan_path, output):
    confined(root, [plan_path, output])
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    campaign = 'neural-confidence-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        source_plan_sha256=digest(plan_path), source_plan_path=str(plan_path.resolve()), status='incomplete', records=[],
        tools={n: digest(Path(__file__).with_name(n)) for n in ('audit_noise_confidence.py', 'edge_protected_noise.py',
            'run_noisy_moment_pilot.py', 'synthetic_bayer_data.py', 'bayer_canonical.py', 'run_diverse_moment_pilot.py')},
        limits=['Consulted simulated fields; pixels spatially dependent, no confidence interval or camera false-positive calibration.',
            'Constant and affine truths have no interior discontinuity; smooth waves are a separate content diagnostic.',
            'Core16x16 packed cells matches the32x32 full-resolution reconstruction core.'])
    guard, started = MemoryGuard(**plan['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            lookup = {(r['case_index'], r['phase'], r['noise_index']): r for r in plan['observations']}
            for index, case in enumerate(plan['mixed']+plan['edges']):
                truth = signal(case['family'], case['seed'], 128)[None] if 'family' in case else edge_image(case)
                if tensor_hash(truth) != case['target_sha256']:
                    raise ValueError('Frozen truth differs')
                for phase, values in enumerate(PATTERNS):
                    tags = torch.tensor([values])
                    for level in (1, 2):
                        guard.check()
                        frozen = lookup[index, phase, level]
                        packed, sigma = observation(truth, tags, level, frozen['noise_seed'])
                        if tensor_hash(packed) != frozen['noisy_sha256'] or tensor_hash(sigma) != frozen['sigma_sha256']:
                            raise ValueError('Frozen observation differs')
                        cp, cs, _, _ = canonicalize(packed/plan['config']['scale'], sigma/plan['config']['scale'], tags)
                        confidence = edge_confidence(cp, cs)[:, :, 24:-24, 24:-24]
                        result['records'].append(dict(family=case.get('family', 'edge'), case_index=index,
                            phase_index=phase, noise_index=level, target_sha256=case['target_sha256'],
                            noisy_sha256=frozen['noisy_sha256'], sigma_sha256=frozen['sigma_sha256'],
                            cells=confidence.numel(), confidence_sum=float(confidence.sum()),
                            any_attenuation=int((confidence < 1).sum()), below_half=int((confidence < .5).sum()),
                            minimum=float(confidence.min()), maximum=float(confidence.max())))
            families = sorted({r['family'] for r in result['records']})
            result['summary'] = {}
            for level in (1, 2):
                result['summary'][str(level)] = {}
                for family in families:
                    rows = [r for r in result['records'] if r['noise_index'] == level and r['family'] == family]
                    cells = sum(r['cells'] for r in rows)
                    result['summary'][str(level)][family] = dict(cases=len(rows), cells=cells,
                        mean_confidence=sum(r['confidence_sum'] for r in rows)/cells,
                        fraction_attenuated=sum(r['any_attenuation'] for r in rows)/cells,
                        fraction_below_half=sum(r['below_half'] for r in rows)/cells)
            result['status'] = 'completed_confidence_audit_not_photographic_validation'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(result['summary']), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.plan, args.output)
