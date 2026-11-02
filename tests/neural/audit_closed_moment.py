"""Bench qualification of a transpose-closed basis, without training or GPU changes."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation, fixed_prediction, difference, aggregate_differences
from bayer_canonical import canonicalize, spatial_reflect
from closed_moment_operator import CLOSED_STENCILS, CHANNELS, CHANNEL_COUNT, TRANSPOSE_PERMUTATION, interpolate_closed, transpose_coefficients
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


def uniform_prediction(packed, pattern):
    cp, _, labels, flags = canonicalize(packed, torch.zeros_like(packed), pattern)
    coefficients = cp.new_full((len(cp), CHANNEL_COUNT, *cp.shape[-2:]), .25)
    return spatial_reflect(interpolate_closed(cp, labels, coefficients), flags)


def run(parent, output):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Expected completed source for existing32 contours')
    tools = {**proof['freeze']['tools'], **{name: digest(Path(__file__).with_name(name)) for name in
        ('closed_moment_operator.py', 'audit_bayer_symmetries.py', 'audit_closed_moment.py')}}
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Source implementation changed')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    now = datetime.now(timezone.utc)
    campaign = 'neural-closed-moment-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(schema_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        source_result_sha256=digest(parent/'result.json'), tools=tools,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        channels=CHANNEL_COUNT, stencils=CLOSED_STENCILS, transpose_permutation=TRANSPOSE_PERMUTATION,
        fixed_coefficients=.25, field_seed_base=104000, core_border=48,
        fixed_symmetry_absolute_tolerance=1e-6, arbitrary_field_absolute_tolerance=2e-6,
        policy='Same32 consulted contours/4phases; fixed weights and correctly permuted random fields; no neural head, fit or quality selection.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    records, fields = [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=records, coefficient_field_records=fields, status='incomplete',
        compute=dict(device='cpu', threads_intra=4, torch=torch.__version__, cuda_compiled=torch.version.cuda,
            cuda_available=torch.cuda.is_available(),
            hardware_gpu_observation='nvidia-smi: RTX3060,12288MiB,driver610.88; CPU-only installed PyTorch; no environment change.'),
        limits=['Operator audit only: no trained36-channel neural head or denoising implementation.',
            'Uniform.25 weights differ in total weight at green sites after adding candidates; not a quality comparison.',
            'Covariant coefficients must permute with their taps; support closure alone cannot guarantee learned equivariance.',
            '32 already consulted synthetic contours, not independent natural-scene validation.',
            'No full-definition latency/memory measure or GPU benchmark.'])
    started = time.monotonic()
    try:
        if len(CHANNELS) != len(set(TRANSPOSE_PERMUTATION)) or any(
                TRANSPOSE_PERMUTATION[TRANSPOSE_PERMUTATION[i]] != i for i in range(CHANNEL_COUNT)):
            raise ValueError('Transpose channel mapping is not an involutive bijection')
        with torch.inference_mode():
            for case in proof['freeze']['reserved_edges']:
                truth = edge_image(case, proof['freeze']['config']['side'])
                if tensor_hash(truth) != case['target_sha256']:
                    raise ValueError('Source truth changed')
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    packed = sample_rgb(truth, pattern)
                    before = tensor_hash(packed)
                    if before != case['packed_sha256_by_phase'][phase]:
                        raise ValueError('Source observation changed')
                    original = fixed_prediction(packed, pattern, 'all_candidates')
                    closed = uniform_prediction(packed, pattern)
                    for op in OPERATIONS:
                        tp, tags = transform_observation(packed, pattern, op)
                        restored = transform(uniform_prediction(tp, tags), op, inverse=True)
                        old = transform(fixed_prediction(tp, tags, 'all_candidates'), op, inverse=True)
                        measured = difference(closed, restored)
                        if measured['max_absolute_difference'] > 1e-6 or not torch.equal(sample_rgb(restored, pattern), packed):
                            raise ValueError('Closed fixed operator symmetry or sensor copy failed')
                        records.append(dict(seed=case['seed'], phase_index=phase, operation=list(op),
                            packed_sha256=before, original=difference(original, old), closed=measured))
                    cp, _, labels, _ = canonicalize(packed, torch.zeros_like(packed), pattern)
                    field_seed = 104000+len(fields)
                    coeff = torch.rand((1, CHANNEL_COUNT, *cp.shape[-2:]), generator=torch.Generator().manual_seed(field_seed))*2-1
                    base = interpolate_closed(cp, labels, coeff)
                    tp, tags = transform_observation(cp, labels, (3, True))
                    correct = interpolate_closed(tp, tags, transpose_coefficients(coeff)).transpose(-2, -1)
                    unmapped = interpolate_closed(tp, tags, coeff.transpose(-2, -1)).transpose(-2, -1)
                    measured = difference(base, correct)
                    if measured['max_absolute_difference'] > 2e-6 or not torch.equal(sample_rgb(base, labels), cp):
                        raise ValueError('Arbitrary coefficient covariance or sensor copy failed')
                    if not torch.equal(transpose_coefficients(transpose_coefficients(coeff)), coeff):
                        raise ValueError('Coefficient roundtrip failed')
                    fields.append(dict(seed=case['seed'], phase_index=phase, field_seed=field_seed,
                        coefficients_sha256=tensor_hash(coeff), mapped=measured, unmapped=difference(base, unmapped)))
                    if tensor_hash(packed) != before:
                        raise ValueError('Source modified')
        result['summary'] = dict(original_fixed=aggregate_differences([r['original'] for r in records]),
            closed_fixed=aggregate_differences([r['closed'] for r in records]),
            correctly_mapped_fields=aggregate_differences([r['mapped'] for r in fields]),
            unmapped_fields_negative_control=aggregate_differences([r['unmapped'] for r in fields]),
            channels=CHANNEL_COUNT, missing_transposed_candidates=0)
        if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
            raise ValueError('Study code changed')
        result['status'] = 'completed_transpose_closed_operator_audit_not_trained_model'
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
