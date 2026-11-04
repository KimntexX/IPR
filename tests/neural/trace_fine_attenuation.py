"""Known fine-signal diagnosis: CFA front versus reconstruction, without fitting."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract, response
from bayer_canonical import canonicalize, spatial_reflect
from closed_moment_operator import interpolate_closed
from confirm_fine_noise import fine_signal, observation
from compare_reconstruction_objectives import confined
from context_data import extend_observation, crop_centre
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet, edge_confidence
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from noisy_moment_unet import NoisyMomentUNet
from run_noisy_moment_pilot import NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from trace_noise_reconstruction import coefficients


def run(root, output, proof_path, history_path):
    confined(root, [output, proof_path, history_path])
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    source = source_contract(root, proof)
    check_inference(source['data'], source['tools'])
    history = json.loads(history_path.read_text(encoding='utf-8'))
    plan = history['plan']
    if history['status'] != 'completed_fine_signal_regression_not_photographic_qualification':
        raise ValueError('Complete consulted fine regression required')
    originals = {(r['seed'], r['case_index'], r['phase_index'], r['noise_index'], r['replicate']): r
        for r in history['records']}
    now = datetime.now(timezone.utc)
    identity = 'neural-fine-trace-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', proof_sha256=digest(proof_path), history_sha256=digest(history_path),
        script_sha256=digest(Path(__file__)), records=[],
        selection='All strong textures, phase0, realization0, both positive levels, both seeds; known controls only.',
        limits=['Counterfactual decomposition is descriptive, not a qualified replacement.',
            'Only one Bayer phase and noise realization per texture are diagnosed.'])
    output.mkdir(parents=True, exist_ok=False)
    guard, started = MemoryGuard(**source['data']['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in source['models']:
                path = Path(ref['folder'])/'weights.pt'
                if digest(path) != ref['weights_sha256']:
                    raise ValueError('Weights changed')
                model = EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                model.load_state_dict(torch.load(path, weights_only=True)['state_dict'], strict=True)
                for field in plan['fields']:
                    case = plan['cases'][field['case_index']]
                    if case['amplitude'] != .06 or field['phase_index'] != 0 or field['replicate'] != 0 or field['noise_index'] == 0:
                        continue
                    guard.check()
                    truth, wave, quadrature = fine_signal(case)
                    expanded, _, _ = fine_signal(case, 384, -96)
                    if tensor_hash(truth) != case['target_sha256'] or not torch.equal(crop_centre(expanded, 192), truth):
                        raise ValueError('Historical target changed')
                    tags = torch.tensor([PATTERNS[0]])
                    _, observed, sigma = observation(truth, tags, field)
                    if tensor_hash(observed) != field['observation_sha256'] or tensor_hash(sigma) != field['sigma_sha256']:
                        raise ValueError('Historical noise changed')
                    packed, sigma = extend_observation(expanded, tags, observed, sigma,
                        *NOISE[field['noise_index']], 210000000+field['noise_seed'])
                    values, sigma, labels, flags = canonicalize(packed/2, sigma/2, tags)
                    raw = NoisyMomentUNet.correct_observations(model, values, sigma, labels)
                    corrected = model.correct_observations(values, sigma, labels)
                    before = coefficients(model, values, labels)
                    after = coefficients(model, corrected, labels)
                    original = interpolate_closed(values, labels, before)
                    full = interpolate_closed(corrected, labels, after)
                    if not torch.equal(full, model(values, sigma, labels)):
                        raise ValueError('Diagnostic mirror differs from full forward')
                    variants = dict(parent=original, full=full,
                        frozen_coefficients=interpolate_closed(corrected, labels, before),
                        coefficient_only=interpolate_closed(values, labels, after),
                        unprotected=interpolate_closed(raw, labels, coefficients(model, raw, labels)))
                    measures = {name: response(crop_centre(spatial_reflect(image, flags)*2, 192), truth, wave, quadrature)
                        for name, image in variants.items()}
                    old = originals[ref['seed'], field['case_index'], 0, field['noise_index'], 0]
                    if max(abs(a-b) for a,b in zip(measures['full']['sine_gain'], old['full']['sine_gain'])) > 2e-6:
                        raise ValueError('Full reconstruction differs from historical measurement')
                    confidence = crop_centre(edge_confidence(values, sigma), 96)[:, :, 32:-32, 32:-32]
                    result['records'].append(dict(seed=ref['seed'], field=field, case=case, variants=measures,
                        confidence_mean=float(confidence.mean()), confidence_minimum=float(confidence.min()),
                        full_forward_bit_exact=True))
                print('TRACED', ref['seed'], flush=True)
            if len(result['records']) != 144:
                raise ValueError('Expected144paired probes')
            result['status'] = 'completed_known_fine_attenuation_diagnosis_not_qualification'
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE', identity, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--historical', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.proof, args.historical)
