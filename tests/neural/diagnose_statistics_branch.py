"""Read-only within-model ablation of observed CFA statistics on known fields."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import json
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract, response
from bayer_canonical import canonicalize, spatial_reflect
from closed_moment_operator import interpolate_closed
from compare_reconstruction_objectives import confined
from confirm_fine_noise import fine_signal, observation
from context_data import extend_observation, crop_centre
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from run_noisy_moment_pilot import NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from trace_noise_reconstruction import coefficients


@contextmanager
def without_statistics(model):
    """Zero only this branch output; restore hooks even after a failed probe."""
    handle = model.noise_features.statistics.register_forward_hook(
        lambda module, inputs, output: torch.zeros_like(output))
    try:
        yield
    finally:
        handle.remove()


def measure(model, values, sigma, labels, clean, truth, wave, quadrature, flags):
    corrected = model.correct_observations(values, sigma, labels)
    image = interpolate_closed(corrected, labels, coefficients(model, corrected, labels))
    rgb = response(crop_centre(spatial_reflect(image, flags)*2, 192), truth, wave, quadrature)
    residual = crop_centre(corrected-clean, 32).double()
    baseline = crop_centre(values-clean, 32).double().square().mean()
    rgb['cfa_mse_over_noisy'] = float(residual.square().mean()/baseline)
    rgb['cfa_bias_per_plane'] = residual.mean(dim=(0, 2, 3)).tolist()
    return rgb, crop_centre(image, 64), crop_centre(corrected, 32)


def run(root, output, control_proof, candidate_proof, history_path):
    confined(root, [output, control_proof, candidate_proof, history_path])
    history = json.loads(history_path.read_text(encoding='utf-8'))
    if history['status'] != 'completed_fine_signal_regression_not_photographic_qualification':
        raise ValueError('Completed known fine regression required')
    known = {(r['seed'], r['case_index'], r['phase_index'], r['noise_index'], r['replicate']):r
        for r in history['records']}
    sources = {name:source_contract(root, json.loads(path.read_text(encoding='utf-8')))
        for name, path in (('control', control_proof), ('candidate', candidate_proof))}
    for source in sources.values():
        check_inference(source['data'], source['tools'])
        if {ref['seed'] for ref in source['models']} != {71, 73}:
            raise ValueError('Two fixed seeds required')
    now = datetime.now(timezone.utc)
    identity = 'neural-statistics-ablation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', script_sha256=digest(Path(__file__)), history_sha256=digest(history_path),
        control_proof_sha256=digest(control_proof), candidate_proof_sha256=digest(candidate_proof),
        selection='All known amplitudes/frequencies/colours, phase0 realization0, positive noise levels, both seeds and arms.',
        limits=['Within-model intervention; not a separately trained baseline or independent qualification.',
            'One phase and realization; no fitted threshold, no retraining and no reserved data.',
            'Lower CFA MSE can still mean weaker transfer; all RGB/transfer measures retained.'], records=[])
    output.mkdir(parents=True, exist_ok=False)
    guard = MemoryGuard(**sources['candidate']['data']['memory_policy'])
    started = time.monotonic()
    try:
        with guard, torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for arm, source in sources.items():
                for ref in source['models']:
                    weights = Path(ref['folder'])/'weights.pt'
                    if digest(weights) != ref['weights_sha256']:
                        raise ValueError('Final weights changed')
                    model = EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                    model.load_state_dict(torch.load(weights, weights_only=True)['state_dict'], strict=True)
                    for field in history['plan']['fields']:
                        if field['phase_index'] != 0 or field['replicate'] != 0 or field['noise_index'] == 0:
                            continue
                        guard.check()
                        case = history['plan']['cases'][field['case_index']]
                        truth, wave, quadrature = fine_signal(case)
                        expanded, _, _ = fine_signal(case, 384, -96)
                        if tensor_hash(truth) != case['target_sha256'] or not torch.equal(crop_centre(expanded, 192), truth):
                            raise ValueError('Known target changed')
                        tags = torch.tensor([PATTERNS[0]])
                        _, observed, sigma = observation(truth, tags, field)
                        if tensor_hash(observed) != field['observation_sha256'] or tensor_hash(sigma) != field['sigma_sha256']:
                            raise ValueError('Known observation changed')
                        packed, sigma = extend_observation(expanded, tags, observed, sigma,
                            *NOISE[field['noise_index']], 210000000+field['noise_seed'])
                        values, sigma, labels, flags = canonicalize(packed/2, sigma/2, tags)
                        clean, _, _, _ = canonicalize(sample_rgb(expanded, tags)/2, torch.zeros_like(sigma), tags)
                        full, rgb, cfa = measure(model, values, sigma, labels, clean, truth, wave, quadrature, flags)
                        with without_statistics(model):
                            ablated, without_rgb, without_cfa = measure(model, values, sigma, labels, clean, truth, wave, quadrature, flags)
                        if arm == 'candidate':
                            old = known[ref['seed'], field['case_index'], 0, field['noise_index'], 0]['full']
                            if max(abs(a-b) for a,b in zip(full['sine_gain'], old['sine_gain'])) > 2e-6:
                                raise ValueError('Full forward differs from known measurement')
                        if arm == 'control' and (not torch.equal(rgb, without_rgb) or not torch.equal(cfa, without_cfa)):
                            raise ValueError('Frozen-zero control must be bit-identical under ablation')
                        result['records'].append(dict(arm=arm, seed=ref['seed'], case=case, field=field,
                            weights_sha256=ref['weights_sha256'], full=full, without_statistics=ablated,
                            rgb_branch_delta_mse=float((rgb-without_rgb).double().square().mean()),
                            cfa_branch_delta_mse=float((cfa-without_cfa).double().square().mean()),
                            control_bit_exact=arm == 'control'))
                    if model.noise_features.statistics._forward_hooks:
                        raise ValueError('Diagnostic hook leaked')
                    print('ABLATION', arm, ref['seed'], flush=True)
            if len(result['records']) != 576:
                raise ValueError('Expected 576 fixed paired observations')
            result['status'] = 'completed_known_statistics_ablation_not_qualification'
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        result['memory'] = guard.stats()
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{identity}.json', result)
    print(result['status'], flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'control-proof', 'candidate-proof', 'history'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2], args.output, args.control_proof, args.candidate_proof, args.history)
