"""Exact-observation D4 audit: coordinate plumbing, fixed supports, frozen network."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch
from torch.nn import functional as F

from bayer_canonical import canonicalize, spatial_reflect, predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import bilinear, sample_rgb
from moment_bayer_unet import STENCILS, interpolate_weights, model_from_config
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


OPERATIONS = tuple((k, mirror) for mirror in (False, True) for k in range(4))
OPERATORS = ('bilinear', 'zero_weights', 'symmetric_candidates', 'all_candidates', 'learned')


def transform(image, operation, inverse=False):
    if operation not in OPERATIONS or image.ndim != 4:
        raise ValueError('NCHW field and declared D4 operation required')
    k, mirror = operation
    if inverse:
        return torch.rot90(image.flip((-1,)) if mirror else image, -k, (-2, -1))
    result = torch.rot90(image, k, (-2, -1))
    return result.flip((-1,)) if mirror else result


def transform_observation(packed, pattern, operation):
    if packed.ndim != 4 or packed.shape[1] != 4 or pattern.shape != packed.shape[:2]:
        raise ValueError('N4HW spatial planes and matching N4 tags required')
    transformed = F.pixel_unshuffle(transform(F.pixel_shuffle(packed, 2), operation), 2)
    labels = transform(pattern.reshape(-1, 1, 2, 2), operation).reshape(-1, 4)
    return transformed, labels


def fixed_prediction(packed, pattern, kind):
    if kind not in OPERATORS[:4]:
        raise ValueError('Declared fixed operator required')
    if kind == 'bilinear':
        return bilinear(packed, pattern)
    cp, _, labels, flags = canonicalize(packed, torch.zeros_like(packed), pattern)
    coefficients = cp.new_zeros(len(cp), 8, 4, *cp.shape[-2:])
    if kind == 'symmetric_candidates':
        coefficients[:, :, :2] = .25
    elif kind == 'all_candidates':
        coefficients[:] = .25
    output = interpolate_weights(cp, labels, coefficients)
    return spatial_reflect(output, flags)


def transpose_supports():
    """List closure only: absence does not prove absence from the linear span."""
    signature = lambda s: tuple(sorted((dy, dx, weight) for dy, dx, weight in s))
    lookup = {(c, y, x): {signature(s) for s in supports} for c, y, x, supports in STENCILS}
    rows = []
    for colour, y, x, supports in STENCILS:
        for i, support in enumerate(supports):
            mapped = signature([(dx, dy, w) for dy, dx, w in support])
            rows.append(dict(colour=colour, site=[y, x], candidate=i,
                transposed_site=[x, y], transposed_taps=mapped, present=mapped in lookup[colour, x, y]))
    return rows


def difference(reference, output, border=48):
    if (reference.shape != output.shape or min(reference.shape[-2:]) <= 2*border
            or not torch.isfinite(reference).all() or not torch.isfinite(output).all()):
        raise ValueError('Finite matching outputs with sufficient common core required')
    delta = output[:, :, border:-border, border:-border]-reference[:, :, border:-border, border:-border]
    return dict(components=delta.numel(), squared_difference_sum=float(delta.square().sum()),
        max_absolute_difference=float(delta.abs().max()), mean_squared_difference=float(delta.square().mean()))


def aggregate_differences(rows):
    return dict(cases=len(rows), components=sum(r['components'] for r in rows),
        mean_squared_difference=sum(r['squared_difference_sum'] for r in rows)/sum(r['components'] for r in rows),
        max_absolute_difference=max(r['max_absolute_difference'] for r in rows))


def run(parent, output):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    weights = parent/'synthetic-joint-weights.pt'
    if (proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification'
            or digest(weights) != proof['weights_sha256']):
        raise ValueError('Expected frozen completed diversity experiment')
    tools = {**proof['freeze']['tools'], 'audit_bayer_symmetries.py': digest(Path(__file__))}
    for name, expected in tools.items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed model or sampling implementation: '+name)
    config = proof['freeze']['config']
    if config['model_kind'] != 'moment' or len(proof['freeze']['reserved_edges']) != 32:
        raise ValueError('Expected moment model and32 existing reserved contents')
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
    now = datetime.now(timezone.utc)
    campaign = 'neural-bayer-symmetries-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(source_result_sha256=digest(parent/'result.json'), weights_sha256=digest(weights), config=config,
        tools=tools, operations=OPERATIONS, operators=OPERATORS,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        core_border=48, fixed_coefficient=.25, plumbing_absolute_tolerance=1e-6,
        policy='Transform the identical full-resolution CFA and2x2 tags, never re-sample new sensor sites; invert output geometry.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    rows = []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=rows, support_transposition=transpose_supports(), status='incomplete',
        versions=dict(torch=torch.__version__), limits=[
            '32 already consulted synthetic contents times4 phases times8 transforms; correlated diagnostic cases.',
            'Equivariance disagreement is not reconstruction error or perceptual degradation.',
            'Fixed coefficient witnesses diagnose operators, not the full achievable set of learned combinations.',
            'Support-list closure failure does not prove a missing linear span or cause all learned errors.',
            'No new training, model selection, decoder change or photographic qualification.'])
    expected = {(r['seed'], tuple(r['pattern'])): r for r in proof['groups']['reserved']}
    started = time.monotonic()
    try:
        with torch.inference_mode():
            for case in proof['freeze']['reserved_edges']:
                truth = edge_image(case, config['side'])
                if tensor_hash(truth) != case['target_sha256']:
                    raise ValueError('Source truth changed')
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    packed = sample_rgb(truth, pattern)
                    before = tensor_hash(packed)
                    if before != case['packed_sha256_by_phase'][phase]:
                        raise ValueError('Source observation changed')

                    def predictions(p, tags):
                        return {**{kind: fixed_prediction(p, tags, kind) for kind in OPERATORS[:4]},
                            'learned': predict(model, p/config['scale'], torch.zeros_like(p), tags,
                                exact_observations=True, canonical_input=True)*config['scale']}

                    baseline = predictions(packed, pattern)
                    if tensor_hash(baseline['learned']) != expected[case['seed'], tuple(values)]['output_sha256']:
                        raise ValueError('Frozen learned source prediction differs')
                    for operation in OPERATIONS:
                        tp, tags = transform_observation(packed, pattern, operation)
                        # General inverse includes a mirror before inverse rotation.
                        inverse_mosaic = transform(F.pixel_shuffle(tp, 2), operation, inverse=True)
                        inverse_tags = transform(tags.reshape(1, 1, 2, 2), operation, inverse=True).reshape(1, 4)
                        if not torch.equal(F.pixel_unshuffle(inverse_mosaic, 2), packed) or not torch.equal(inverse_tags, pattern):
                            raise ValueError('Exact observation/tag roundtrip failed')
                        if not torch.equal(tp, sample_rgb(transform(truth, operation), tags)):
                            raise ValueError('Transformed CFA/tag correspondence failed')
                        outputs = predictions(tp, tags)
                        measured = {}
                        for name, image in outputs.items():
                            image = transform(image, operation, inverse=True)
                            if not torch.equal(sample_rgb(image, pattern), packed):
                                raise ValueError('Observed samples changed')
                            measured[name] = difference(baseline[name], image)
                            if name in ('bilinear', 'zero_weights') and measured[name]['max_absolute_difference'] > 1e-6:
                                raise ValueError('Symmetric baseline/plumbing tolerance exceeded')
                        rows.append(dict(seed=case['seed'], phase_index=phase, pattern=list(values), operation=list(operation),
                            target_sha256=case['target_sha256'], packed_sha256=before,
                            transformed_packed_sha256=tensor_hash(tp), transformed_pattern=tags[0].tolist(),
                            exact_roundtrip=True, observed_sites_exact=True, operators=measured))
                    if tensor_hash(packed) != before:
                        raise ValueError('Source modified')
        result['summary'] = {name: aggregate_differences([r['operators'][name] for r in rows]) for name in OPERATORS}
        result['by_operation'] = {str(op): {name: aggregate_differences([r['operators'][name] for r in rows if tuple(r['operation']) == op])
            for name in OPERATORS} for op in OPERATIONS}
        if digest(weights) != freeze['weights_sha256'] or any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
            raise ValueError('Model or study implementation changed')
        result['status'] = 'completed_exact_cfa_symmetry_audit_no_training'
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
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.output)
