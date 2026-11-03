"""Eligibility-gated finer-signal confirmation with observation-level resume."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract, response
from bayer_canonical import predict
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import sample_rgb
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from neural_inference_contract import INFERENCE_TOOLS, check_inference
from prepare_closed_inference import prepare_inference
from run_noisy_moment_pilot import NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


TOOLS = tuple(dict.fromkeys(INFERENCE_TOOLS+('confirm_fine_noise.py', 'audit_spectral_noise.py',
    'neural_inference_contract.py', 'synthetic_bayer_data.py', 'run_synthetic_joint_pilot.py',
    'joint_bayer_unet.py', 'memory_budget.py')))


def fine_signal(case):
    if (not all(isinstance(case[n], (int, float)) and not isinstance(case[n], bool)
            and math.isfinite(case[n]) for n in ('frequency', 'amplitude', 'angle', 'phase'))
            or not 0 < case['frequency'] < 32 or not 0 < case['amplitude'] <= .12
            or not 0 <= case['angle'] < math.pi or not isinstance(case['chromatic'], bool)):
        raise ValueError('Finite sub-CFA-Nyquist periodic signal parameters required')
    x = torch.arange(192, dtype=torch.float32)
    y, x = torch.meshgrid(x, x, indexing='ij')
    angle = case['angle']
    phase = (x*math.cos(angle)+y*math.sin(angle))*(2*math.pi*case['frequency']/128)+case['phase']
    wave = phase.sin()
    weights = torch.tensor((1., -1., .5) if case['chromatic'] else (1., 1., 1.))
    return (.3+case['amplitude']*weights[:, None, None]*wave)[None], wave, phase.cos()


def observation(truth, tags, field):
    clean = sample_rgb(truth, tags)
    a, b = NOISE[field['noise_index']]
    observed = clean+torch.randn(clean.shape, generator=torch.Generator().manual_seed(field['noise_seed']))*(a*clean.clamp_min(0)+b).sqrt()
    return clean, observed, (a*observed.clamp_min(0)+b).sqrt()


def prepare(root, proof_path, output):
    confined(root, [proof_path, output])
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    if proof.get('pilot_gates_passed') is not True:
        raise ValueError('Successful two-seed source required before opening new fine contents')
    source = source_contract(root, proof)
    check_inference(source['data'], source['tools'])
    if len(source['models']) != 2 or {r['seed'] for r in source['models']} != {71, 73}:
        raise ValueError('Both seeds required')
    cases, fields = [], []
    guard = MemoryGuard(**source['data']['memory_policy'])
    with guard:
        torch.set_num_threads(4)
        for frequency in (6, 10, 14, 18, 22, 26):
            for amplitude in (.008, .06):
                for angle in (.17, .63, 1.07):
                    for chromatic in (False, True):
                        guard.check()
                        index = len(cases)
                        case = dict(frequency=frequency, amplitude=amplitude, angle=angle,
                            chromatic=chromatic, phase=1.17)
                        truth, _, _ = fine_signal(case)
                        case['target_sha256'] = tensor_hash(truth)
                        cases.append(case)
                        for phase, p in enumerate(PATTERNS):
                            tags = torch.tensor([p])
                            for level in range(3):
                                for replicate in range(1 if level == 0 else 3):
                                    field = dict(case_index=index, phase_index=phase, noise_index=level,
                                        replicate=replicate, noise_seed=130000000+index*1000+phase*100+level*10+replicate)
                                    _, observed, sigma = observation(truth, tags, field)
                                    fields.append(dict(field, observation_sha256=tensor_hash(observed), sigma_sha256=tensor_hash(sigma)))
    if len({c['target_sha256'] for c in cases}) != 72:
        raise ValueError('Distinct72fine signals required')
    output.mkdir(parents=True, exist_ok=False)
    plan = dict(schema_version=1, frozen_at_utc=datetime.now(timezone.utc).isoformat(), source=source,
        source_proof_path=str(proof_path.resolve()), source_proof_sha256=digest(proof_path), cases=cases, fields=fields,
        tools={n: digest(Path(__file__).with_name(n)) for n in TOOLS}, preparation_memory=guard.stats(),
        criteria=dict(strong_amplitude=.06, strong_minimum_channel_gain_over_parent=.95,
            strong_rgb_and_sensor_maximum_ratio=1.000001, overall_rgb_maximum_ratio=.98),
        policy='Both fixed seeds, all cases; no fitting, threshold change or seed selection after opening.',
        limits=['Procedural content only, not camera/photographic qualification.',
            'Weak0.008amplitude transfer fully reported but not required to be unbiased at low SNR.',
            'Three positive-noise fields paired across models; zero-noise inputs are not independent replicates.'])
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def key(row):
    return tuple(row[n] for n in ('seed', 'case_index', 'phase_index', 'noise_index', 'replicate'))


def reuse_records(prior, plan, plan_hash):
    if prior['status'] != 'aborted_for_memory_budget' or prior['plan_sha256'] != plan_hash:
        raise ValueError('Only exact-plan memory stops may resume')
    json.dumps(prior, allow_nan=False)
    fields = {(f['case_index'], f['phase_index'], f['noise_index'], f['replicate']): f for f in plan['fields']}
    seen = set()
    for row in prior['records']:
        identity = key(row)
        if identity in seen or identity[0] not in (71, 73) or identity[1:] not in fields:
            raise ValueError('Duplicate or undeclared saved observation')
        field = fields[identity[1:]]
        if (row['observation_sha256'] != field['observation_sha256'] or row['sigma_sha256'] != field['sigma_sha256']
                or row['target_sha256'] != plan['cases'][row['case_index']]['target_sha256']):
            raise ValueError('Saved fine-content identity changed')
        seen.add(identity)
    return prior['records']


def summarize(rows, criteria):
    if not rows:
        raise ValueError('Nonempty fine group required')
    full = [sum(r['full']['sine_gain'][c] for r in rows)/len(rows) for c in range(3)]
    parent = [sum(r['parent']['sine_gain'][c] for r in rows)/len(rows) for c in range(3)]
    if any(v <= .05 for v in parent):
        raise ValueError('Reference fine-signal transfer too small to compare safely')
    gain = [a/b for a, b in zip(full, parent)]
    rgb = sum(r['full']['rgb_mse'] for r in rows)/sum(r['parent']['rgb_mse'] for r in rows)
    sensor = sum(r['sensor']['corrected_mse'] for r in rows)/sum(r['sensor']['noisy_mse'] for r in rows)
    checks = dict(channel_transfer=min(gain) >= criteria['strong_minimum_channel_gain_over_parent'],
        rgb_not_worse=rgb <= criteria['strong_rgb_and_sensor_maximum_ratio'],
        sensor_not_worse=sensor <= criteria['strong_rgb_and_sensor_maximum_ratio'])
    return dict(cases=len(rows), sine_gain=full, parent_sine_gain=parent, gain_over_parent=gain,
        rgb_over_parent=rgb, sensor_over_observed=sensor, strong_checks=checks,
        strong_passed=all(checks.values()))


def run(root, plan_path, output, reuse=None):
    confined(root, [plan_path, output]+([reuse] if reuse else []))
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
        raise ValueError('Frozen fine confirmation changed')
    source = plan['source']
    check_inference(source['data'], source['tools'])
    proof_path = Path(plan['source_proof_path'])
    confined(root, [proof_path])
    if digest(proof_path) != plan['source_proof_sha256']:
        raise ValueError('Frozen source proof changed')
    plan_hash = digest(plan_path)
    rows = [] if reuse is None else reuse_records(json.loads(reuse.read_text(encoding='utf-8')), plan, plan_hash)
    done = {key(r) for r in rows}
    now = datetime.now(timezone.utc)
    identity = 'neural-fine-confirmation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', plan=plan, plan_sha256=plan_hash, records=rows,
        reused_result_sha256=digest(reuse) if reuse else None, reused_observations=len(rows))
    guard, start = MemoryGuard(**source['data']['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in source['models']:
                path = Path(ref['folder'])/'weights.pt'
                p = next(p for p in source['data']['parents'] if p['seed'] == ref['seed'])
                parent_path = Path(p['folder'])/'weights.pt'
                confined(root, [path, parent_path])
                if digest(path) != ref['weights_sha256'] or digest(parent_path) != p['weights_sha256']:
                    raise ValueError('Frozen weights changed')
                model = EdgeProtectedNoisyUNet(source['data']['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(torch.load(path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                model = prepare_inference(model)
                parent = LocalMomentUNet(source['data']['config']['width']).eval().requires_grad_(False)
                parent.load_state_dict(torch.load(parent_path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                parent = prepare_inference(parent)
                cached_case_index = None
                for field in plan['fields']:
                    row_key = (ref['seed'], field['case_index'], field['phase_index'], field['noise_index'], field['replicate'])
                    if row_key in done:
                        continue
                    guard.check()
                    case = plan['cases'][field['case_index']]
                    if cached_case_index != field['case_index']:
                        truth, wave, cosine = fine_signal(case)
                        if tensor_hash(truth) != case['target_sha256']:
                            raise ValueError('Frozen fine target changed')
                        cached_case_index = field['case_index']
                    tags = torch.tensor([PATTERNS[field['phase_index']]])
                    clean, observed, sigma = observation(truth, tags, field)
                    if (tensor_hash(observed) != field['observation_sha256']
                            or tensor_hash(sigma) != field['sigma_sha256']):
                        raise ValueError('Frozen fine observation changed')
                    full = predict(model, observed/2, sigma/2, tags, False, True)*2
                    base = predict(parent, observed/2, torch.zeros_like(sigma), tags, True, True)*2
                    if field['noise_index'] == 0 and not torch.equal(full, base):
                        raise ValueError('Noise-free parent changed')
                    before = (observed-clean)[:, :, 32:-32, 32:-32]
                    after = (sample_rgb(full, tags)-clean)[:, :, 32:-32, 32:-32]
                    rows.append(dict(field, seed=ref['seed'], target_sha256=case['target_sha256'],
                        full=response(full, truth, wave, cosine), parent=response(base, truth, wave, cosine),
                        sensor=dict(noisy_mse=float(before.square().mean()), corrected_mse=float(after.square().mean()))))
                    done.add(row_key)
                print('CONFIRMED seed'+str(ref['seed']), flush=True)
                del model, parent
            if len(rows) != 4032:
                raise ValueError('Missing fine confirmation observations')
            rows.sort(key=key)
            result['groups'] = []
            for seed in (71, 73):
                for level in (1, 2):
                    selected = [r for r in rows if r['seed'] == seed and r['noise_index'] == level]
                    ratio = sum(r['full']['rgb_mse'] for r in selected)/sum(r['parent']['rgb_mse'] for r in selected)
                    result.setdefault('overall', []).append(dict(seed=seed, noise_index=level,
                        rgb_over_parent=ratio, passed=ratio <= plan['criteria']['overall_rgb_maximum_ratio']))
                    for frequency in (6, 10, 14, 18, 22, 26):
                        for amplitude in (.008, .06):
                            for chromatic in (False, True):
                                group = [r for r in selected if all(plan['cases'][r['case_index']][n] == v for n, v in
                                    (('frequency', frequency), ('amplitude', amplitude), ('chromatic', chromatic)))]
                                result['groups'].append(dict(seed=seed, noise_index=level, frequency=frequency,
                                    amplitude=amplitude, chromatic=chromatic, **summarize(group, plan['criteria'])))
            result['pilot_gates_passed'] = (all(r['passed'] for r in result['overall']) and all(g['strong_passed']
                for g in result['groups'] if g['amplitude'] == plan['criteria']['strong_amplitude']))
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
                raise ValueError('Fine confirmation changed during inference')
            result['status'] = 'completed_fine_signal_confirmation_not_photographic_qualification'
    except (BudgetExceeded, MemoryError) as error:
        result['status'] = 'aborted_for_memory_budget'
        result['error'] = str(error)
        raise
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-start
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE '+identity+' gates='+str(result['pilot_gates_passed']), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--proof', type=Path)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--reuse', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        if args.proof is None:
            parser.error('Eligible frozen source proof required')
        prepare(root, args.proof, args.output)
    elif args.plan is None:
        parser.error('Frozen fine confirmation plan required')
    else:
        run(root, args.plan, args.output, args.reuse)
