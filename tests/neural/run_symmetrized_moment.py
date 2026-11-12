"""Frozen two-view transpose average; reconstruction, D4 consistency and CPU cost."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import time

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation, difference, aggregate_differences
from bayer_canonical import predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear, project_exact_observations
from moment_bayer_unet import model_from_config
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import angular_signal, summaries
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import metrics, tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def ordinary(model, packed, pattern, scale):
    return predict(model, packed/scale, torch.zeros_like(packed), pattern,
        exact_observations=True, canonical_input=True)*scale


def average_views(first, transposed_back, packed, pattern):
    if first.shape != transposed_back.shape or not torch.isfinite(first).all() or not torch.isfinite(transposed_back).all():
        raise ValueError('Finite matching RGB views required')
    # Sensor sites are projected explicitly, including possible subnormal inputs.
    average = first*.5+transposed_back*.5
    return project_exact_observations(average, packed, torch.zeros_like(packed), pattern)


def both_predictions(model, packed, pattern, scale):
    first = ordinary(model, packed, pattern, scale)
    tp, tags = transform_observation(packed, pattern, (3, True))  # exact transpose
    second = ordinary(model, tp, tags, scale).transpose(-2, -1)
    return first, average_views(first, second, packed, pattern)


def timing(model, inputs, scale):
    """Interleaved wall times, no hashes/metrics in timed calls; tiny CPU crops only."""
    functions = dict(ordinary=lambda p, tags: ordinary(model, p, tags, scale),
        symmetrized=lambda p, tags: both_predictions(model, p, tags, scale)[1])
    for _ in range(3):
        for function in functions.values():
            function(*inputs[0])
    samples = {name: [] for name in functions}
    for repetition in range(7):
        order = tuple(functions) if repetition % 2 == 0 else tuple(reversed(functions))
        for name in order:
            started = time.perf_counter()
            for p, tags in inputs:
                functions[name](p, tags)
            samples[name].append((time.perf_counter()-started)/len(inputs))
    medians = {name: statistics.median(values) for name, values in samples.items()}
    return dict(seconds_per_128px_image_by_repetition=samples, median_seconds=medians,
        median_ratio=medians['symmetrized']/medians['ordinary'], cases_per_repetition=len(inputs),
        warmups=3, repetitions=7, threads_intra=4,
        scope='CPU128x128 synthetic inputs, includes coordinate transforms/projection, excludes I/O/hashes/metrics; not full RAW latency.')


def run(parent, output):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    config = proof['freeze']['config']
    weights = parent/'synthetic-joint-weights.pt'
    if (proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification'
            or digest(weights) != proof['weights_sha256'] or config['model_kind'] != 'moment'):
        raise ValueError('Expected frozen completed diverse moment model')
    tools = {**proof['freeze']['tools'], 'audit_bayer_symmetries.py': digest(Path(__file__).with_name('audit_bayer_symmetries.py')),
        Path(__file__).name: digest(Path(__file__))}
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Changed source implementation')
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
    campaign = 'neural-symmetrized-moment-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(source_result_sha256=digest(parent/'result.json'), weights_sha256=digest(weights), config=config,
        tools=tools, gates=proof['freeze']['gates'], average_weights=[.5, .5], operations=OPERATIONS,
        symmetry_absolute_tolerance=1e-6, core_border=48,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        policy='Fixed weights: same observation plus its exact transpose; equal averaging, no fit, clipping or phase selection.',
        benchmark='Three warmups, seven interleaved32-input runs, CPU4threads, original vs two-view average.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    groups = {n: [] for n in ('crossed', 'angular', 'reserved')}
    rows, symmetries, bench_inputs = [], [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, records=rows, groups=groups, symmetry_records=symmetries, status='incomplete',
        versions=dict(torch=torch.__version__), limits=[
            'All procedural controls already consulted; no new reserved or photographic validation.',
            'Two forward passes, not a new trained checkpoint or production integration.',
            'Equivariance alone does not prove better reconstruction, denoising or perceptual quality.',
            'CPU timing on128px inputs cannot predict full-definition RAW or GPU latency.'])
    started = time.monotonic()
    try:
        with torch.inference_mode():
            tasks = [('crossed', case, signal(case['family'], case['seed'], config['side'])[None])
                for case in proof['freeze']['evaluation_cases']]
            tasks += [('angular', case, angular_signal(case['normal_degrees'], case['offset'], config['side']))
                for case in proof['groups']['angular'] if case['phase_name'] == 'RGGB']
            tasks += [('reserved', case, edge_image(case, config['side'])) for case in proof['freeze']['reserved_edges']]
            for group, case, truth in tasks:
                expected = [r for r in proof['groups'][group] if r.get('seed') == case.get('seed')
                    and (group != 'angular' or (r['normal_degrees'] == case['normal_degrees'] and r['offset'] == case['offset']))]
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    packed = sample_rgb(truth, pattern)
                    before = tensor_hash(packed)
                    original, sym = both_predictions(model, packed, pattern, config['scale'])
                    previous = next(r for r in expected if r['pattern'] == list(values))
                    if (tensor_hash(original) != previous['output_sha256'] or before != previous['packed_sha256']
                            or tensor_hash(truth) != previous['target_sha256']):
                        raise ValueError('Frozen ordinary prediction/input replay differs')
                    if not torch.equal(sample_rgb(sym, pattern), packed):
                        raise ValueError('Observed sites changed')
                    measured = metrics(truth, bilinear(packed, pattern), sym)
                    row = dict(family=case.get('family', 'edge'), pattern=list(values), phase_index=phase,
                        seed=case.get('seed'), normal_degrees=case.get('normal_degrees'), offset=case.get('offset'),
                        target_sha256=tensor_hash(truth), packed_sha256=before, original_output_sha256=tensor_hash(original),
                        output_sha256=tensor_hash(sym), metrics=measured, reference_metrics=previous['metrics'], observed_site_mse=0.)
                    groups[group].append(row)
                    if group == 'crossed' and list(values) == case['pattern']:
                        rows.append(row)
                    if group == 'reserved':
                        if phase == 0:
                            bench_inputs.append((packed, pattern))
                        for op in OPERATIONS:
                            tp, tags = transform_observation(packed, pattern, op)
                            restored = transform(both_predictions(model, tp, tags, config['scale'])[1], op, inverse=True)
                            score = difference(sym, restored)
                            if score['max_absolute_difference'] > 1e-6 or not torch.equal(sample_rgb(restored, pattern), packed):
                                raise ValueError('Symmetrized D4 consistency or observation tolerance exceeded')
                            symmetries.append(dict(seed=case['seed'], phase_index=phase, operation=list(op), **score))
                    if tensor_hash(packed) != before:
                        raise ValueError('Source changed')
            result.update(summaries(rows))
            result['qualification'] = qualifies(result, freeze['gates'])
            result['summaries'] = {name: dict(**summaries(records),
                reference=summaries([dict(family=r['family'], metrics=r['reference_metrics']) for r in records]),
                by_phase={str(p): summaries([r for r in records if r['phase_index'] == p])['summary'] for p in range(4)})
                for name, records in groups.items()}
            result['crossed_qualification'] = qualifies(dict(records=groups['crossed'], **summaries(groups['crossed'])), freeze['gates'])
            result['symmetry_summary'] = aggregate_differences(symmetries)
            result['timing'] = timing(model, bench_inputs, config['scale'])
        if digest(weights) != freeze['weights_sha256'] or any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
            raise ValueError('Model or implementation changed during measurement')
        result['status'] = 'completed_frozen_transpose_average_not_photographic_qualification'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(dict(qualification=result['qualification'],
        scores={n: r['summary'] for n, r in result['summaries'].items()},
        symmetry=result['symmetry_summary'], timing=result['timing']), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.output)
