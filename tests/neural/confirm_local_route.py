"""Frozen new-content confirmation of both local-route seeds with bounded memory."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import time

import torch

from bayer_canonical import predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from prepare_closed_inference import prepare_inference
from reconstruction_gates import qualifies
from run_diverse_moment_pilot import edge_recipe, freeze_edge, edge_image
from run_synthetic_joint_pilot import tensor_hash, metrics, aggregate
from synthetic_bayer_data import FAMILIES, PATTERNS, signal


MODEL_TOOLS = ('local_moment_unet.py', 'closed_moment_unet.py', 'closed_moment_operator.py',
    'bayer_canonical.py', 'joint_bayer_unet.py', 'camera_unet.py', 'synthetic_bayer_data.py',
    'run_memory_bounded_reconstruction.py', 'run_balanced_moment_pilot.py', 'run_diverse_moment_pilot.py')


def prepare(root, source, output):
    source, output = source.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (source, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((source/'result.json').read_text(encoding='utf-8'))
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Completed frozen source data required')
    guard = MemoryGuard()
    with guard:
        torch.set_num_threads(4)
        edges = [freeze_edge(edge_recipe(i, 'reserved'), 128) for i in range(128, 160)]
        mixed = [dict(family=f, seed=3100000+100*i+j, original_phase_index=j)
            for i, f in enumerate(FAMILIES) for j in range(4)]
        training_hashes = {r['target_sha256'] for r in proof['freeze']['train_edge_events']}
        for case in proof['freeze']['train_slots']:
            guard.check()
            training_hashes.add(tensor_hash(signal(case['family'], case['seed'], 128)[None]))
        for case in mixed:
            guard.check()
            truth = signal(case['family'], case['seed'], 128)[None]
            case['target_sha256'] = tensor_hash(truth)
            case['packed_sha256_by_phase'] = [tensor_hash(sample_rgb(truth, torch.tensor([p]))) for p in PATTERNS]
        hashes = [r['target_sha256'] for r in edges+mixed]
        if len(set(hashes)) != 52 or training_hashes.intersection(hashes):
            raise ValueError('Confirmation target collision or training overlap')
        plan = dict(schema_version=1, frozen_at_utc=datetime.now(timezone.utc).isoformat(), edges=edges, mixed=mixed,
            source_result_sha256=digest(source/'result.json'), seeds=[71, 73], steps=2048,
            tools={n: digest(Path(__file__).with_name(n)) for n in MODEL_TOOLS+('confirm_local_route.py', 'prepare_closed_inference.py', 'memory_budget.py')},
            policy='Structure chosen on earlier development data; new truths frozen before second fit, excluded from optimization and checkpoint choice.')
        output.mkdir(parents=True, exist_ok=False)
    plan['preparation_memory'] = guard.stats()
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def summarize(rows, gates):
    data = dict(summary=aggregate(rows), by_family={f: aggregate([r for r in rows if r['family'] == f]) for f in FAMILIES})
    return dict(**data, qualification=qualifies(dict(records=rows, **data), gates))


def run(root, plan_path, source, models, output):
    source, output, plan_path = source.resolve(), output.resolve(), plan_path.resolve()
    models = [p.resolve() for p in models]
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in models+[source, output, plan_path]):
        raise ValueError('Use dedicated neural build')
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
        raise ValueError('Confirmation/model implementation changed after freeze')
    proofs = [json.loads((p/'result.json').read_text(encoding='utf-8')) for p in models]
    if len(models) != 2 or {p['freeze']['config']['seed'] for p in proofs} != {71, 73} or any(
            p['status'] != 'completed_local_route_pilot_not_noise_training' or p['freeze']['config']['model_kind'] != 'local'
            or p['freeze']['config']['steps'] != 2048 or p['freeze']['source_result_sha256'] != plan['source_result_sha256'] for p in proofs):
        raise ValueError('Completed local seeds71/73 with identical source and budget required')
    for proof in proofs:
        # Training orchestrator versions may differ; computation/data implementations must not.
        if any(n not in proof['freeze']['tools'] or proof['freeze']['tools'][n] != plan['tools'][n] for n in MODEL_TOOLS):
            raise ValueError('Different model implementation between checkpoints')
    if ({k: v for k, v in proofs[0]['freeze']['config'].items() if k != 'seed'}
            != {k: v for k, v in proofs[1]['freeze']['config'].items() if k != 'seed'}):
        raise ValueError('Independent seeds must share the same numerical configuration')
    source_proof = json.loads((source/'result.json').read_text(encoding='utf-8'))
    if digest(source/'result.json') != plan['source_result_sha256']:
        raise ValueError('Source changed')
    if any(p['freeze']['gates'] != source_proof['freeze']['gates'] for p in proofs):
        raise ValueError('Qualification thresholds changed')
    now = datetime.now(timezone.utc)
    campaign = 'neural-local-confirmation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        plan=plan, plan_sha256=digest(plan_path), models=[], status='incomplete',
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        limits=['One new cohort from existing procedural generators, not photographic/camera/noise validation.',
            'Truths frozen after first model/architecture choice; before second training and confirmation inference.',
            'Both seeds retained, no best-seed or checkpoint selection.',
            'Prepared inference timings use4cached128px inputs cycled32calls, not full-definition deployment performance.'])
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
                if digest(folder/'weights.pt') != proof['weights_sha256']:
                    raise ValueError('Weights changed')
                checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
                if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
                    raise ValueError('Checkpoint provenance differs')
                model = LocalMomentUNet(config['width'])
                model.load_state_dict(checkpoint['state_dict'], strict=True)
                model.eval()
                model.requires_grad_(False)
                tick = time.perf_counter()
                prepared = prepare_inference(model)
                preparation_seconds = time.perf_counter()-tick
                rows, edges, dev, original20, bit_checks, timing_inputs = [], [], [], [], [], []
                def evaluate(truth, metadata, expected=None):
                    answers = []
                    for phase, p in enumerate(PATTERNS):
                        guard.check()
                        tags = torch.tensor([p])
                        packed = sample_rgb(truth, tags)
                        packed_hash = tensor_hash(packed)
                        reference = predict(model, packed/config['scale'], torch.zeros_like(packed), tags, True, True)*config['scale']
                        image = predict(prepared, packed/config['scale'], torch.zeros_like(packed), tags, True, True)*config['scale']
                        if not torch.equal(reference, image) or not torch.equal(sample_rgb(image, tags), packed):
                            raise ValueError('Prepared output or sensor identity differs')
                        if 'target_sha256' in metadata and (tensor_hash(truth) != metadata['target_sha256'] or packed_hash != metadata['packed_sha256_by_phase'][phase]):
                            raise ValueError('Frozen confirmation observation changed')
                        image_hash = tensor_hash(image)
                        if expected is not None and (expected[phase]['output_sha256']['full'] != image_hash
                                or expected[phase]['target_sha256'] != tensor_hash(truth) or expected[phase]['packed_sha256'] != packed_hash):
                            raise ValueError('Development prediction replay differs')
                        row = dict(family=metadata['family'], seed=metadata['seed'], phase_index=phase,
                            target_sha256=tensor_hash(truth), packed_sha256=packed_hash, output_sha256=image_hash,
                            metrics=metrics(truth, bilinear(packed, tags), image), observed_site_mse=0.)
                        answers.append(row)
                        bit_checks.append(image_hash)
                        if len(timing_inputs) < 4:
                            timing_inputs.append((packed, tags))
                    return answers
                with torch.inference_mode():
                    for case in plan['mixed']:
                        answers = evaluate(signal(case['family'], case['seed'], 128)[None], case)
                        rows.extend(answers)
                        original20.append(answers[case['original_phase_index']])
                    for case in plan['edges']:
                        edges.extend(evaluate(edge_image(case), dict(family='edge', **case)))
                    for case in source_proof['freeze']['evaluation_cases']:
                        expected = [next(r for r in proof['records'] if r['family'] == case['family'] and r['seed'] == case['seed'] and r['phase_index'] == p) for p in range(4)]
                        dev.extend(evaluate(signal(case['family'], case['seed'], 128)[None], case, expected))
                    functions = dict(projected=model, prepared=prepared)
                    samples = {name: [] for name in functions}
                    for net in functions.values():
                        p, tags = timing_inputs[0]
                        for _ in range(3):
                            predict(net, p/config['scale'], torch.zeros_like(p), tags, True, True)
                    for repetition in range(7):
                        for name in (tuple(functions) if repetition % 2 == 0 else tuple(reversed(functions))):
                            guard.check()
                            tick = time.perf_counter()
                            for i in range(32):
                                p, tags = timing_inputs[i % 4]
                                predict(functions[name], p/config['scale'], torch.zeros_like(p), tags, True, True)*config['scale']
                            samples[name].append((time.perf_counter()-tick)/32)
                if any(not torch.equal(v, model.state_dict()[n]) for n, v in checkpoint['state_dict'].items()):
                    raise ValueError('Source model mutated')
                result['models'].append(dict(seed=config['seed'], source_result_sha256=digest(folder/'result.json'), weights_sha256=proof['weights_sha256'],
                    mixed_records=rows, edge_records=edges, development_records=dev,
                    mixed=summarize(rows, proof['freeze']['gates']), original20=summarize(original20, proof['freeze']['gates']),
                    development=summarize(dev, proof['freeze']['gates']), edges=aggregate(edges),
                    prepared_bit_exact_cases=len(bit_checks), preparation_seconds=preparation_seconds, timing_samples=samples,
                    timing_median_seconds={n: statistics.median(v) for n, v in samples.items()}))
                del prepared, model, checkpoint
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
                raise ValueError('Study changed during confirmation')
            result['status'] = 'completed_local_route_confirmation_and_preparation_no_noise_training'
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
    print(json.dumps([dict(seed=m['seed'], mixed=m['mixed']['summary']['mse_ratio'], edges=m['edges']['mse_ratio'],
        checks=m['mixed']['qualification'], prepared_cases=m['prepared_bit_exact_cases'], timing=m['timing_median_seconds']) for m in result['models']], indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepare', action='store_true')
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--plan', type=Path)
    p.add_argument('--models', nargs=2, type=Path)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    if a.prepare:
        prepare(root, a.source, a.output)
    elif a.plan is None or a.models is None:
        p.error('Confirmation requires --plan and two --models')
    else:
        run(root, a.plan.resolve(), a.source, a.models, a.output)
