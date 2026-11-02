"""Frozen-head ablations: full fields, bias-only and spatial mean coefficients."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from bayer_canonical import canonicalize, spatial_reflect, predict
from closed_moment_operator import interpolate_closed
from closed_moment_unet import build_model, TransposeConv
from diagnose_train_learnability import digest, save
from joint_bayer_unet import bilinear, sample_rgb
from memory_budget import MemoryGuard, BudgetExceeded
from run_synthetic_joint_pilot import tensor_hash, metrics, aggregate
from synthetic_bayer_data import PATTERNS, FAMILIES, signal


VARIANTS = ('full', 'bias_only', 'image_mean', 'core_mean')


def coefficient_fields(logits, effective_bias, border=24):
    if (not isinstance(logits, torch.Tensor) or not isinstance(effective_bias, torch.Tensor)
            or logits.ndim != 4 or logits.shape[1] != 36 or min(logits.shape[-2:]) <= 2*border
            or effective_bias.shape != (36,) or effective_bias.device != logits.device
            or effective_bias.dtype != logits.dtype or not torch.isfinite(logits).all()
            or not torch.isfinite(effective_bias).all()):
        raise ValueError('Finite N36HW logits, matching36bias and sufficient core required')
    full = logits.tanh()
    core = full[:, :, border:-border, border:-border]
    image_mean = full.mean(dim=(2, 3), keepdim=True)
    core_mean = core.mean(dim=(2, 3), keepdim=True)
    bias = effective_bias.tanh()[None, :, None, None].expand_as(full)
    energy = float(core.square().mean())
    spatial_energy = float((core-core_mean).square().mean())
    stats = dict(mean_abs_coefficient_core=float(core.abs().mean()),
        mean_abs_difference_from_bias_core=float((core-bias[:, :, border:-border, border:-border]).abs().mean()),
        mean_channel_spatial_std_core=float(core.std(dim=(2, 3), unbiased=False).mean()),
        core_spatial_energy_fraction=spatial_energy/energy if energy > 1e-20 else None,
        coefficient_energy_core=energy, spatial_energy_core=spatial_energy,
        image_mean_by_channel=image_mean.flatten().tolist(), core_mean_by_channel=core_mean.flatten().tolist())
    return dict(full=full, bias_only=bias, image_mean=image_mean.expand_as(full), core_mean=core_mean.expand_as(full)), stats


def summarize(records):
    return {v: aggregate([dict(metrics=r['metrics'][v]) for r in records]) for v in VARIANTS}


def run(models, source, output):
    root = Path(__file__).resolve().parents[2]
    models, source, output = [p.resolve() for p in models], source.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in models+[source, output]):
        raise ValueError('Use dedicated neural build')
    source_proof = json.loads((source/'result.json').read_text(encoding='utf-8'))
    cases = source_proof['freeze']['evaluation_cases']
    if len(cases) != 20 or any(c.get('noise_index', 0) != 0 for c in cases):
        raise ValueError('Expected20 existing noise-free contents')
    proofs = [json.loads((p/'result.json').read_text(encoding='utf-8')) for p in models]
    allowed = ('completed_closed_unet_pilot_not_photographic_qualification', 'completed_memory_bounded_reconstruction_not_noise_training')
    for folder, proof in zip(models, proofs):
        if (proof['status'] not in allowed or proof['freeze']['config']['model_kind'] != 'equivariant'
                or digest(folder/'weights.pt') != proof['weights_sha256']):
            raise ValueError('Expected completed frozen equivariant checkpoint')
        if any(digest(Path(__file__).with_name(n)) != h for n, h in proof['freeze']['tools'].items()):
            raise ValueError('Changed model dependency')
    now = datetime.now(timezone.utc)
    campaign = 'neural-coefficient-adaptation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(source_result_sha256=digest(source/'result.json'),
        model_results_sha256=[digest(p/'result.json') for p in models],
        weights_sha256=[p['weights_sha256'] for p in proofs], variants=VARIANTS,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        code_sha256=digest(Path(__file__)), coefficient_core_border=24, rgb_core_border=48,
        policy='Means AFTER tanh, no target-based weight choice; full-image mean primary, core mean boundary witness. No fit or changed gate.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(), freeze=freeze,
        models=[], status='incomplete', limits=[
            'Already consulted procedural20contents crossed with4phases; no new independent scene or photo.',
            'Bias-only is an ablation, not a separately optimized fixed filter.',
            'Mean coefficients are image-conditioned; core mean excludes a fixed boundary but is not target-selected.',
            'MSE/output differences do not measure perceptual changes or prove useful/inutile network capacity.'])
    guard = MemoryGuard()
    started = time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for folder, proof in zip(models, proofs):
                guard.check()
                config = proof['freeze']['config']
                checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
                if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
                    raise ValueError('Checkpoint provenance differs')
                model = build_model('equivariant', config['width'])
                model.load_state_dict(checkpoint['state_dict'], strict=True)
                model.requires_grad_(False)
                model.eval()
                if not isinstance(model.head, TransposeConv):
                    raise ValueError('Projected head representation required')
                bias = model.head.bias*.5+model.head.bias[model.head.output_permutation]*.5
                captured = {}
                records = []
                model_result = dict(source_campaign=proof['campaign_id'], steps=config['steps'], seed=config['seed'],
                    records=records, bias_coefficients=bias.tanh().tolist())
                result['models'].append(model_result)
                expected_rows = proof.get('crossed_records', proof.get('groups', {}).get('crossed', []))
                expected = {(r['family'], r['seed'], tuple(r['pattern'])): r for r in expected_rows}
                if len(expected) != 80:
                    raise ValueError('Expected80 source replay identities')
                handle = model.head.register_forward_hook(lambda module, args, value: captured.update(logits=value.detach()))
                try:
                    with torch.inference_mode():
                        for case in cases:
                            target = signal(case['family'], case['seed'], config['side'])[None]
                            for phase, values in enumerate(PATTERNS):
                                guard.check()
                                pattern = torch.tensor([values])
                                packed = sample_rgb(target, pattern)
                                before = tensor_hash(packed)
                                image = predict(model, packed/config['scale'], torch.zeros_like(packed), pattern, True, True)*config['scale']
                                previous = expected[case['family'], case['seed'], tuple(values)]
                                if (tensor_hash(image) != previous['output_sha256'] or before != previous['packed_sha256']
                                        or tensor_hash(target) != previous['target_sha256']):
                                    raise ValueError('Full prediction/input replay differs')
                                fields, stats = coefficient_fields(captured.pop('logits'), bias)
                                cp, _, labels, flags = canonicalize(packed/config['scale'], torch.zeros_like(packed), pattern)
                                base = bilinear(packed, pattern)
                                measured, hashes, differences = {}, {}, {}
                                for variant, coefficients in fields.items():
                                    output_image = spatial_reflect(interpolate_closed(cp, labels, coefficients), flags)*config['scale']
                                    if variant == 'full' and not torch.equal(output_image, image):
                                        raise ValueError('Head capture reconstruction differs')
                                    if not torch.equal(sample_rgb(output_image, pattern), packed):
                                        raise ValueError('Observed sites changed')
                                    measured[variant] = metrics(target, base, output_image)
                                    hashes[variant] = tensor_hash(output_image)
                                    difference = (output_image-image)[:, :, 48:-48, 48:-48]
                                    differences[variant] = dict(mean_squared_difference=float(difference.square().mean()),
                                        max_absolute_difference=float(difference.abs().max()))
                                if tensor_hash(packed) != before:
                                    raise ValueError('Source modified')
                                records.append(dict(family=case['family'], seed=case['seed'], phase_index=phase,
                                    original_phase=list(values) == case['pattern'], pattern=list(values),
                                    target_sha256=previous['target_sha256'], packed_sha256=before, output_sha256=hashes,
                                    metrics=measured, output_differences=differences, coefficient_stats=stats))
                        model_result['summary'] = summarize(records)
                        model_result['original20'] = summarize([r for r in records if r['original_phase']])
                        model_result['by_family'] = {f: summarize([r for r in records if r['family'] == f]) for f in FAMILIES}
                finally:
                    handle.remove()
                    captured.clear()
                if any(not torch.equal(v, model.state_dict()[n]) for n, v in checkpoint['state_dict'].items()):
                    raise ValueError('Source model state changed')
                del model, checkpoint
            guard.check()
            if digest(Path(__file__)) != freeze['code_sha256'] or any(digest(p/'weights.pt') != h for p, h in zip(models, freeze['weights_sha256'])):
                raise ValueError('Model or study changed during audit')
            result['status'] = 'completed_frozen_coefficient_adaptation_audit_no_training'
    except (BudgetExceeded, MemoryError) as error:
        result['status'] = 'aborted_for_memory_budget'
        result['error'] = str(error)
        raise
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps([dict(steps=m['steps'], summary=m['summary'], original20=m['original20']) for m in result['models']], indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models', type=Path, nargs=2, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    run(a.models, a.source, a.output)
