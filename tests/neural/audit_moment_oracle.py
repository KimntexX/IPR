"""Optimistic RGB-MSE bound of fixed Bayer supports; synthetic truth only.

Independent coefficients in the CLOSED box [-1,1] are allowed per component.
This is the closure of tanh, not a realizable learned predictor. Opponent and
gradient scores of the RGB minimizer are descriptive, not their lower bounds.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch
from torch.nn import functional as F

from audit_bayer_phases import PHASE_NAMES
from bayer_canonical import canonicalize, spatial_reflect, predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import bilinear, sparse_rgb, sample_rgb
from moment_bayer_unet import STENCILS, interpolate_weights, model_from_config
from run_synthetic_joint_pilot import metrics, aggregate, tensor_hash
from synthetic_bayer_data import FAMILIES, signal


def box_witness(differences, residual):
    """Solve one scalar target with four independently bounded coefficients."""
    if (differences.ndim != residual.ndim + 1 or differences.shape[1] != 4
            or differences.shape[:1] + differences.shape[2:] != residual.shape
            or differences.dtype != residual.dtype or differences.device != residual.device
            or not differences.is_floating_point()
            or not torch.isfinite(differences).all() or not torch.isfinite(residual).all()):
        raise ValueError('Finite matching residuals and four candidate differences required')
    radius = differences.abs().sum(dim=1)
    if not torch.isfinite(radius).all():
        raise ValueError('Reachable interval overflow')
    safe_radius = torch.where(radius > 0, radius, torch.ones_like(radius))
    fraction = (residual / safe_radius).clamp(-1, 1)
    fraction = torch.where(radius > 0, fraction, torch.zeros_like(fraction))
    coefficients = fraction[:, None] * differences.sign()
    optimal = torch.maximum(-radius, torch.minimum(radius, residual))
    return coefficients, optimal, radius


def oracle(packed, pattern, target):
    """Build an oracle witness and verify it through the actual interpolator."""
    base = bilinear(packed, pattern)
    if target.shape != base.shape or not torch.isfinite(target).all():
        raise ValueError('Finite matching synthetic RGB truth required')
    observed, _ = sparse_rgb(packed, pattern)
    padded = F.pad(observed, (3, 3, 3, 3), mode='reflect')
    n, _, h, w = packed.shape
    coefficients = packed.new_zeros(n, 8, 4, h, w)
    expected = base.clone()
    radii, residuals = [], []
    for i, (colour, y, x, supports) in enumerate(STENCILS):
        candidates = []
        for support in supports:
            a, b = [padded[:, colour, 3+dy:3+dy+2*h, 3+dx:3+dx+2*w][:, y::2, x::2]
                    for dy, dx, _ in support]
            # Match the frozen implementation's floating-point operation order.
            candidates.append(a*.5+b*.5 if support[0][2] == .5 else a+(a-b)*.5)
        centre = base[:, colour, y::2, x::2]
        residual = target[:, colour, y::2, x::2] - centre
        coeff, optimal, radius = box_witness(torch.stack(candidates, dim=1)-centre[:, None], residual)
        coefficients[:, i] = coeff
        expected[:, colour, y::2, x::2] = centre + optimal
        radii.append(radius)
        residuals.append(residual)
    output = interpolate_weights(packed, pattern, coefficients)
    torch.testing.assert_close(output, expected, rtol=2e-6, atol=5e-7)
    if not torch.equal(sample_rgb(output, pattern), packed):
        raise ValueError('Observed samples changed')
    return output, torch.stack(radii, dim=1), torch.stack(residuals, dim=1), coefficients


def summarize(rows):
    result = {name: aggregate([dict(metrics=r[name]) for r in rows]) for name in ('learned', 'oracle')}
    total = sum(r['missing_components'] for r in rows)
    result.update(missing_components=total,
        unreachable_fraction=sum(r['unreachable'] for r in rows)/total,
        unreachable_fraction_at_1e_7=sum(r['unreachable_at_1e_7'] for r in rows)/total,
        zero_radius_fraction=sum(r['zero_radius'] for r in rows)/total)
    return result


def run(parent, phase_audit, output):
    root = Path(__file__).resolve().parents[2]
    parent, phase_audit, output = [p.resolve() for p in (parent, phase_audit, output)]
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, phase_audit, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    phase = json.loads((phase_audit/'result.json').read_text(encoding='utf-8'))
    config = proof['freeze']['config']
    weights = parent/'synthetic-joint-weights.pt'
    if (proof['status'] != 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
            or config['model_kind'] != 'moment' or not config['exact_observations'] or not config['canonical_input']
            or phase['status'] != 'completed_crossed_bayer_phase_audit_no_training'
            or phase['freeze']['parent']['result_sha256'] != digest(parent/'result.json')
            or proof['weights_sha256'] != digest(weights)):
        raise ValueError('Expected frozen completed moment pilot and its phase audit')
    for name, expected in phase['freeze']['tools'].items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed phase-audit implementation: '+name)
    if len(phase['records']) != 80:
        raise ValueError('Expected 80 frozen cases')
    now = datetime.now(timezone.utc)
    campaign = 'neural-moment-oracle-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(config=config, parent_result_sha256=digest(parent/'result.json'),
        phase_result_sha256=digest(phase_audit/'result.json'), weights_sha256=digest(weights),
        audit_code_sha256=digest(Path(__file__)), tools=phase['freeze']['tools'],
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        core_border=48, unreachable_diagnostic_tolerance=1e-7,
        ordering_absolute_mse_tolerance=1e-10, witness_rtol=2e-6, witness_atol=5e-7,
        policy='Closed-box independent per-component oracle; RGB objective only; no fit or selection.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    checkpoint = torch.load(weights, map_location='cpu', weights_only=True)
    if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
        raise ValueError('Checkpoint provenance differs')
    model = model_from_config(config)
    model.load_state_dict(checkpoint['state_dict'], strict=True)
    model.requires_grad_(False)
    model.eval()
    rows = []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=rows, status='incomplete', versions=dict(torch=torch.__version__),
        limits=['Synthetic truth consulted per pixel: not deployable or independent photographic validation.',
            'Closed coefficient box is optimistic: tanh endpoints are only limiting values.',
            'Spatial coupling and finite U-Net parameterization ignored; RGB-optimal only.',
            'Same already consulted 20 contents crossed with four phases, no added noise or training.'])
    started = time.monotonic()
    try:
        with torch.inference_mode():
            for old in phase['records']:
                target = signal(old['family'], old['seed'], config['side'])[None]
                pattern = torch.tensor([old['pattern']], dtype=torch.int64)
                packed = sample_rgb(target, pattern)
                if tensor_hash(target) != old['target_sha256'] or tensor_hash(packed) != old['packed_sha256']:
                    raise ValueError('Input identity changed')
                cp, _, pp, flags = canonicalize(packed, torch.zeros_like(packed), pattern)
                bound, radii, residuals, coeff = oracle(cp, pp, spatial_reflect(target, flags))
                bound = spatial_reflect(bound, flags)
                learned = predict(model, packed/config['scale'], torch.zeros_like(packed), pattern,
                    exact_observations=True, canonical_input=True)*config['scale']
                if tensor_hash(learned) != old['output_sha256']:
                    raise ValueError('Learned prediction replay differs')
                base = bilinear(packed, pattern)
                measured = metrics(target, base, bound)
                # Compare mean losses via the shared aggregation contract.
                om = aggregate([dict(metrics=measured)])['output_mse']
                lm = aggregate([dict(metrics=old['metrics'])])
                if om > min(lm['output_mse'], lm['baseline_mse']) + 1e-10:
                    raise ValueError('Oracle is worse than a feasible reconstruction')
                rr = residuals[:, :, 24:-24, 24:-24].abs()
                ss = radii[:, :, 24:-24, 24:-24]
                rows.append(dict(family=old['family'], seed=old['seed'], phase_name=old['phase_name'],
                    pattern=old['pattern'], target_sha256=old['target_sha256'], packed_sha256=old['packed_sha256'],
                    learned_output_sha256=tensor_hash(learned), oracle_output_sha256=tensor_hash(bound),
                    learned=old['metrics'], oracle=measured, missing_components=rr.numel(),
                    unreachable=int((rr > ss).sum()), unreachable_at_1e_7=int((rr > ss+1e-7).sum()),
                    zero_radius=int((ss == 0).sum()), maximum_coefficient=float(coeff.abs().max()),
                    observed_site_exact=True))
                if tensor_hash(packed) != old['packed_sha256']:
                    raise ValueError('Input modified')
        result['summary'] = summarize(rows)
        result['by_family'] = {f: summarize([r for r in rows if r['family'] == f]) for f in FAMILIES}
        result['by_phase'] = {p: summarize([r for r in rows if r['phase_name'] == p]) for p in PHASE_NAMES}
        if digest(weights) != freeze['weights_sha256'] or digest(Path(__file__)) != freeze['audit_code_sha256']:
            raise ValueError('Study changed during measurement')
        result['status'] = 'completed_optimistic_support_bound_no_training'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(result['summary'], indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--phase-audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.phase_audit, args.output)
