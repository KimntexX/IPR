"""Frozen-model extrapolation to finer weak periodic structures; no fitting."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

import torch

from bayer_canonical import predict
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import sample_rgb
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from noisy_moment_unet import FORWARD_TOOLS
from neural_inference_contract import check_inference
from prepare_closed_inference import prepare_inference
from run_noisy_moment_pilot import NOISE, runtime_versions
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


def periodic_signal(frequency, amplitude, orientation, chromatic, side=192):
    if (frequency not in (4, 8, 16, 24) or amplitude not in (.005, .02, .08)
            or orientation not in (0, 1, 2) or not isinstance(chromatic, bool)):
        raise ValueError('Declared spectral audit case required')
    x = torch.arange(side, dtype=torch.float32)
    yy, xx = torch.meshgrid(x, x, indexing='ij')
    coordinate = (xx, yy, (xx+yy)/math.sqrt(2))[orientation]
    phase = coordinate*(2*math.pi*frequency/128)+.37
    wave = phase.sin()
    weights = torch.tensor((1., -1., .5) if chromatic else (1., 1., 1.))
    image = .3+amplitude*weights[:, None, None]*wave
    return image[None], wave, phase.cos()


def response(image, truth, wave, quadrature, border=64):
    if image.shape != truth.shape or not torch.isfinite(image).all():
        raise ValueError('Finite matching image required')
    core = image[0, :, border:-border, border:-border].double().reshape(3, -1)
    target = truth[0, :, border:-border, border:-border].double().reshape(3, -1)
    wave, quadrature = (v[border:-border, border:-border].double().reshape(-1) for v in (wave, quadrature))
    design = torch.stack((torch.ones_like(wave), wave, quadrature), dim=1)
    inverse = torch.linalg.pinv(design)
    coefficients, ideal = inverse@core.T, inverse@target.T
    return dict(rgb_mse=float((core-target).square().mean()),
        sine_gain=(coefficients[1]/ideal[1]).tolist(),
        quadrature_gain=(coefficients[2]/ideal[1]).tolist(),
        dc_error=(coefficients[0]-ideal[0]).tolist())


def source_contract(root, proof):
    if proof['status'] == 'completed_independent_noisy_confirmation_not_photographic_qualification':
        if (proof.get('pilot_gates_passed') is not True or len(proof['groups']) != 6
                or not all(g['evaluation']['pilot_gates_passed'] for g in proof['groups'])):
            raise ValueError('All six independent confirmation groups must pass')
        return proof['plan']
    if proof['status'] == 'completed_fixed_edge_protection_probe_not_photographic_qualification':
        # A diagnosis may inspect a fully measured failed candidate, without
        # opening a new reserved confirmation set or calling it qualified.
        path = Path(proof['plan']['data_plan_path'])
        confined(root, [path])
        if digest(path) != proof['plan']['data_plan_sha256']:
            raise ValueError('Development source plan changed')
        refs = proof['plan']['models']
        if len(proof['models']) != 2:
            raise ValueError('Complete two-seed probe required')
        for ref in refs:
            rows = [r for r in proof['models'] if r['seed'] == ref['seed']]
            if (len(rows) != 1 or rows[0]['learned_weights_sha256'] != ref['weights_sha256']
                    or rows[0]['source_result_sha256'] != ref['result_sha256']):
                raise ValueError('Development weights differ')
        return dict(data=json.loads(path.read_text(encoding='utf-8')), models=refs,
            tools=proof['plan']['tools'], kind='protected')
    raise ValueError('Completed protected probe or successful independent confirmation required')


def run(root, proof_path, output):
    confined(root, [proof_path, output])
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    plan = source_contract(root, proof)
    if plan['kind'] != 'protected' or plan['data']['runtime'] != runtime_versions():
        raise ValueError('Current protected model and pinned runtime required')
    check_inference(plan['data'], plan['tools'])
    refs = plan['models']
    if len(refs) != 2 or {r['seed'] for r in refs} != {71, 73}:
        raise ValueError('Both model seeds required')
    now = datetime.now(timezone.utc)
    campaign = 'neural-spectral-noise-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    freeze = dict(source_proof_sha256=digest(proof_path), source_proof_status=proof['status'],
        source_quality_passed=proof['pilot_gates_passed'], models=refs,
        frequencies_cycles_per_128=[4, 8, 16, 24], amplitudes=[.005, .02, .08],
        orientations=['horizontal', 'vertical', 'diagonal45'], families=['achromatic', 'opponent'],
        noise_replicates={0: 1, 1: 2, 2: 2}, side=192, border=64, scale=2,
        tools={n: digest(Path(__file__).with_name(n)) for n in tuple(dict.fromkeys(
            FORWARD_TOOLS+('edge_protected_noise.py', 'audit_spectral_noise.py', 'prepare_closed_inference.py',
                'neural_inference_contract.py')))},
        limits=['Extrapolation diagnosis, not model selection, fitting or a replacement qualification protocol.',
            'Procedural periodic structure only; no physical sensor or photographic noise truth.',
            'Both seeds and all channels reported; two positive noise draws are paired across models.',
            'Noise-free controls repeated across Bayer phases; fine frequencies also occur inside training edges.'])
    save(output/'freeze.json', freeze)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign,
        measured_at_utc=now.isoformat(), status='incomplete', freeze=freeze,
        freeze_sha256=digest(output/'freeze.json'), records=[])
    guard, start = MemoryGuard(**plan['data']['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in refs:
                folder = Path(ref['folder'])
                parent_ref = next(r for r in plan['data']['parents'] if r['seed'] == ref['seed'])
                parent_path = Path(parent_ref['folder'])/'weights.pt'
                confined(root, [folder, parent_path])
                if digest(folder/'weights.pt') != ref['weights_sha256'] or digest(parent_path) != parent_ref['weights_sha256']:
                    raise ValueError('Qualified weights changed')
                original = EdgeProtectedNoisyUNet(plan['data']['config']['width']).eval().requires_grad_(False)
                state = torch.load(folder/'weights.pt', weights_only=True, map_location='cpu')['state_dict']
                original.load_state_dict(state, strict=True)
                model = prepare_inference(original)
                raw_parent = LocalMomentUNet(plan['data']['config']['width']).eval().requires_grad_(False)
                raw_parent.load_state_dict(torch.load(parent_path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                parent = prepare_inference(raw_parent)
                index = 0
                for frequency in freeze['frequencies_cycles_per_128']:
                    for amplitude in freeze['amplitudes']:
                        for orientation in range(3):
                            for chromatic in (False, True):
                                truth, wave, cosine = periodic_signal(frequency, amplitude, orientation, chromatic)
                                for phase, pattern in enumerate(PATTERNS):
                                    tags = torch.tensor([pattern])
                                    clean = sample_rgb(truth, tags)
                                    for level, (a, b) in enumerate(NOISE):
                                        for replicate in range(freeze['noise_replicates'][level]):
                                            guard.check()
                                            seed = 110000000+index*10000+phase*100+level*10+replicate
                                            noise = torch.randn(clean.shape, generator=torch.Generator().manual_seed(seed))
                                            observed = clean+noise*(a*clean.clamp_min(0)+b).sqrt()
                                            sigma = (a*observed.clamp_min(0)+b).sqrt()
                                            full = predict(model, observed/2, sigma/2, tags, False, True)*2
                                            base = predict(parent, observed/2, torch.zeros_like(sigma), tags, True, True)*2
                                            if level == 0 and not torch.equal(full, base):
                                                raise ValueError('Noise-free parent changed')
                                            # Reconstruction copies corrected CFA sites exactly;
                                            # inspect them separately from interpolated RGB.
                                            corrected = sample_rgb(full, tags)
                                            sensor = lambda v: (v-clean)[:, :, 32:-32, 32:-32]
                                            before, after = sensor(observed), sensor(corrected)
                                            result['records'].append(dict(seed=ref['seed'], case_index=index,
                                                frequency=frequency, amplitude=amplitude, orientation=orientation,
                                                chromatic=chromatic, phase=phase, noise_index=level, replicate=replicate,
                                                noise_seed=seed, target_sha256=tensor_hash(truth),
                                                observation_sha256=tensor_hash(observed), sigma_sha256=tensor_hash(sigma),
                                                sensor=dict(noisy_mse=float(before.square().mean()),
                                                    corrected_mse=float(after.square().mean()),
                                                    corrected_bias_by_physical_position=after.mean(dim=(0, 2, 3)).tolist(),
                                                    pattern=list(pattern)),
                                                full=response(full, truth, wave, cosine),
                                                parent=response(base, truth, wave, cosine)))
                                index += 1
                    print('AUDITED seed'+str(ref['seed'])+' frequency'+str(frequency), flush=True)
                if any(not torch.equal(v, original.state_dict()[k]) for k, v in state.items()):
                    raise ValueError('Weights changed')
                del model, parent, original, raw_parent, state
            if len(result['records']) != 2880:
                raise ValueError('Missing spectral audit cases')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in freeze['tools'].items()):
                raise ValueError('Audit changed during evaluation')
            result['status'] = 'completed_spectral_extrapolation_diagnosis_not_photographic_qualification'
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
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.proof, args.output)
