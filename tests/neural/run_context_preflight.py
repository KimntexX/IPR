"""Eight bounded engineering updates and context checks, no quality verdict."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from bayer_canonical import predict, canonicalize
from clean_cfa_loss import clean_cfa_loss
from compare_reconstruction_objectives import confined
from context_data import extend_texture, extend_observation, crop_centre
from diagnose_train_learnability import digest, save
from fine_texture_training import texture
from joint_bayer_unet import sample_rgb, bilinear
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from multiscale_correction import composed_support
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction
from noisy_training_policy import configure_trainable
from prepare_closed_inference import prepare_inference
from reconstruction_loss import energy_balanced_loss
from run_noisy_moment_pilot import MEMORY_POLICY, TOOLS, observation, NOISE, runtime_versions
from synthetic_bayer_data import PATTERNS


def run(root, parent, output):
    confined(root, [parent, output])
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    weights = parent/'weights.pt'
    if proof['weights_sha256'] != digest(weights):
        raise ValueError('Frozen reconstruction weights changed')
    output.mkdir(parents=True, exist_ok=False)
    now = datetime.now(timezone.utc)
    identity = 'neural-context-preflight-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', support=composed_support(), runtime=runtime_versions(),
        parent_result_sha256=digest(parent/'result.json'), parent_weights_sha256=digest(weights),
        tools={n: digest(Path(__file__).with_name(n)) for n in TOOLS+('run_context_preflight.py',)},
        policy='Eight engineering Adam updates only; weights discarded. No reserved targets, quality gates or early stopping.',
        completed_steps=0, zero_noise_checks=[], covariance_checks=[], window_checks=[])
    guard, start = MemoryGuard(**MEMORY_POLICY), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            torch.manual_seed(71)
            state = torch.load(weights, weights_only=True, map_location='cpu')['state_dict']
            model = NoisyMomentUNet(8)
            load_reconstruction(model, state)
            result['training_scope'] = configure_trainable(model, 'cfa_front')
            result['parameter_count'] = sum(p.numel() for p in model.parameters())
            optimizer = torch.optim.Adam(model.parameters(), lr=.001)
            for step in range(8):
                optimizer.zero_grad(set_to_none=True)
                for phase, pattern in enumerate(PATTERNS):
                    guard.check()
                    serial = 4*step+phase
                    family = 'chromatic_wave' if phase % 2 else 'weak_achromatic_wave'
                    centre, _ = texture(family, serial, 128)
                    truth = extend_texture(family, serial, 256)
                    tags = torch.tensor([pattern])
                    level = serial % 3
                    old_values, old_sigma = observation(centre, tags, level, 74000000+serial)
                    values, sigma = extend_observation(truth, tags, old_values, old_sigma, *NOISE[level], 170000000+serial)
                    image = predict(model, values/2, sigma/2, tags, False, True)
                    reference = bilinear(values/2, tags)
                    loss = energy_balanced_loss(image, truth/2, reference, 2., border=112, objective='tempered')/4
                    if level:
                        clean, dev, labels, _ = canonicalize(sample_rgb(truth, tags)/2, sigma/2, tags)
                        anchor = model.correct_observations(clean, dev, labels)
                        loss = loss+clean_cfa_loss(anchor, clean, reference, truth/2, 2., border=112)/4
                        del clean, dev, labels, anchor
                    if not torch.isfinite(loss):
                        raise ValueError('Nonfinite context preflight loss')
                    if loss.requires_grad:
                        loss.backward()
                    del image, reference, loss, truth, values, sigma, old_values, old_sigma, centre
                if any(p.grad is not None and not torch.isfinite(p.grad).all() for p in model.parameters()):
                    raise ValueError('Nonfinite context preflight gradient')
                optimizer.step()
                result['completed_steps'] = step+1
                print('ENGINEERING STEP '+str(step+1)+'/8', flush=True)
            model.eval().requires_grad_(False)
            prepared = prepare_inference(model)
            parent_model = LocalMomentUNet(8).eval().requires_grad_(False)
            parent_model.load_state_dict(state, strict=True)
            with torch.inference_mode():
                centre, _ = texture('chromatic_wave', 17, 128)
                truth = extend_texture('chromatic_wave', 17, 384)
                for phase, pattern in enumerate(PATTERNS):
                    guard.check()
                    tags = torch.tensor([pattern])
                    original, dev = observation(centre, tags, 2, 74000017)
                    packed, sigma = extend_observation(truth, tags, original, dev, *NOISE[2], 170000017)
                    full = predict(prepared, packed/2, sigma/2, tags, False, True)*2
                    same = predict(model, packed/2, sigma/2, tags, False, True)*2
                    if not torch.equal(full, same):
                        raise ValueError('Prepared multiscale output differs')
                    part = predict(prepared, packed[:, :, 32:-32, 32:-32]/2,
                        sigma[:, :, 32:-32, 32:-32]/2, tags, False, True)*2
                    delta = float((full[:, :, 160:224, 160:224]-part[:, :, 96:160, 96:160]).abs().max())
                    result['window_checks'].append(dict(phase_index=phase, halo=96, max_abs=delta))
                    if delta > 2e-6:
                        raise ValueError('Context support margin insufficient')
                    zero = torch.zeros_like(packed)
                    first = predict(model, packed/2, zero, tags, False, True)*2
                    expected = predict(parent_model, packed/2, zero, tags, True, True)*2
                    if not torch.equal(first, expected) or not torch.equal(sample_rgb(first, tags), packed):
                        raise ValueError('Zero-noise identity broken')
                    result['zero_noise_checks'].append(dict(phase_index=phase, bit_exact=True))
                    old_parent = predict(parent_model, original/2, torch.zeros_like(original), tags, True, True)*2
                    context_parent = crop_centre(expected)
                    bridge = float((old_parent-context_parent)[:, :, 48:-48, 48:-48].abs().max())
                    if bridge > 2e-6:
                        raise ValueError('Historical parent scoring core changed')
                    result.setdefault('parent_core_checks', []).append(dict(phase_index=phase, max_abs=bridge))
                    for op in OPERATIONS:
                        guard.check()
                        tp, labels = transform_observation(packed, tags, op)
                        ts, _ = transform_observation(sigma, tags, op)
                        restored = transform(predict(prepared, tp/2, ts/2, labels, False, True)*2, op, True)
                        error = float((restored-full)[:, :, 160:224, 160:224].abs().max())
                        result['covariance_checks'].append(dict(phase_index=phase, operation=list(op), max_abs=error))
                        if error > 2e-6:
                            raise ValueError('Context covariance tolerance exceeded')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in result['tools'].items()):
                raise ValueError('Preflight implementation changed during measurement')
            result['status'] = 'passed_multiscale_engineering_preflight_not_quality_qualification'
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
    print('COMPLETE '+identity, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.parent, args.output)
