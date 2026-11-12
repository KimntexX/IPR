"""Diagnose CFA correction versus interpolation-coefficient changes, no fitting."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch
from torch.nn import functional as F

from audit_spectral_noise import source_contract
from bayer_canonical import canonicalize, spatial_reflect
from closed_moment_operator import interpolate_closed
from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import colour_tags
from local_moment_unet import LocalMomentUNet, local_descriptors
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference, INFERENCE_TOOLS
from run_noisy_moment_pilot import observation
from run_synthetic_joint_pilot import metrics, aggregate, tensor_hash
from synthetic_bayer_data import signal, PATTERNS


def coefficients(model, packed, labels):
    """Diagnostic mirror, admitted only after bit-exact full-forward checks."""
    tags = colour_tags(labels).reshape(len(packed), 12, 1, 1).expand(-1, -1, *packed.shape[2:])
    a = model.enc1(torch.cat((packed, torch.zeros_like(packed), tags), dim=1))
    b = model.enc2(F.avg_pool2d(a, 2))
    c = model.middle(F.avg_pool2d(b, 2))
    d = model.dec2(torch.cat((F.interpolate(c, size=b.shape[2:], mode='nearest'), b), dim=1))
    e = model.dec1(torch.cat((F.interpolate(d, size=a.shape[2:], mode='nearest'), a), dim=1))
    return (model.head(e)+model.local_head(model.local_features(local_descriptors(packed)))).tanh()


def run(root, proof_path, history_path, output):
    confined(root, [proof_path, history_path, output])
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    source = source_contract(root, proof)
    check_inference(source['data'], source['tools'])
    history = json.loads(history_path.read_text(encoding='utf-8'))
    data = history['plan']['data']
    if history['status'] != 'completed_independent_noisy_confirmation_not_photographic_qualification':
        raise ValueError('Known complete cohort required')
    selected = {i: c for i, c in enumerate(data['mixed']) if c['family'] == 'edge'}
    if len(selected) != 4:
        raise ValueError('All four mixed edges required, no case selection')
    now = datetime.now(timezone.utc)
    identity = 'neural-reconstruction-trace-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', source_proof_sha256=digest(proof_path), historical_result_sha256=digest(history_path),
        models=source['models'], tools={n: digest(Path(__file__).with_name(n)) for n in INFERENCE_TOOLS+('trace_noise_reconstruction.py',)},
        records=[], limits=['Known failing family selected after measurement; diagnosis, not independent qualification.',
            'Frozen-coefficient and coefficient-only variants are counterfactual probes, not trained candidates.'])
    guard, start = MemoryGuard(**source['data']['memory_policy']), time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in source['models']:
                path = Path(ref['folder'])/'weights.pt'
                confined(root, [path])
                if digest(path) != ref['weights_sha256']:
                    raise ValueError('Source weights changed')
                model = EdgeProtectedNoisyUNet(source['data']['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(torch.load(path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                for replica in history['plan']['replicas']:
                    for field in replica['observations']:
                        if field['case_index'] not in selected or field['noise_index'] != 2:
                            continue
                        guard.check()
                        case = selected[field['case_index']]
                        truth = signal(case['family'], case['seed'], 128)[None]
                        if tensor_hash(truth) != case['target_sha256']:
                            raise ValueError('Known target changed')
                        tags = torch.tensor([PATTERNS[field['phase']]])
                        packed, sigma = observation(truth, tags, 2, field['noise_seed'])
                        if tensor_hash(packed) != field['noisy_sha256'] or tensor_hash(sigma) != field['sigma_sha256']:
                            raise ValueError('Known noise field changed')
                        values, deviations, labels, flags = canonicalize(packed/2, sigma/2, tags)
                        corrected = model.correct_observations(values, deviations, labels)
                        original_coefficients = coefficients(model, values, labels)
                        corrected_coefficients = coefficients(model, corrected, labels)
                        original = interpolate_closed(values, labels, original_coefficients)
                        full = interpolate_closed(corrected, labels, corrected_coefficients)
                        if (not torch.equal(original, LocalMomentUNet.forward(model, values, torch.zeros_like(values), labels, True))
                                or not torch.equal(full, model(values, deviations, labels))):
                            raise ValueError('Diagnostic coefficient mirror differs from full model')
                        variants = dict(full=full,
                            frozen_coefficients=interpolate_closed(corrected, labels, original_coefficients),
                            coefficient_only=interpolate_closed(values, labels, corrected_coefficients))
                        base = spatial_reflect(original, flags)*2
                        result['records'].append(dict(seed=ref['seed'], replicate=replica['index'],
                            case_index=field['case_index'], phase_index=field['phase'], target_sha256=case['target_sha256'],
                            noisy_sha256=field['noisy_sha256'], full_forward_bit_exact=True,
                            metrics={n: metrics(truth, base, spatial_reflect(v, flags)*2) for n, v in variants.items()}))
                del model
            if len(result['records']) != 96:
                raise ValueError('Missing paired reconstruction probes')
            result['summaries'] = {str(seed): {n: aggregate([dict(metrics=r['metrics'][n]) for r in result['records'] if r['seed'] == seed])
                for n in ('full', 'frozen_coefficients', 'coefficient_only')} for seed in (71, 73)}
            result['status'] = 'completed_reconstruction_dependency_diagnosis_not_qualification'
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-start
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print('COMPLETE '+identity, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--historical', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.proof, args.historical, args.output)
