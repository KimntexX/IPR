"""Frozen paired confirmation: RGB versus energy-balanced loss, both seeds."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from bayer_canonical import predict
from confirm_local_route import MODEL_TOOLS, summarize
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from prepare_closed_inference import prepare_inference
from run_diverse_moment_pilot import edge_recipe, freeze_edge, edge_image
from run_synthetic_joint_pilot import tensor_hash, metrics, aggregate
from synthetic_bayer_data import FAMILIES, PATTERNS, signal


TOOLS = MODEL_TOOLS+('compare_reconstruction_objectives.py', 'confirm_local_route.py',
    'prepare_closed_inference.py', 'memory_budget.py', 'reconstruction_gates.py',
    'run_synthetic_joint_pilot.py')


def model_contract(proofs):
    """Only declared loss and seed may differ; archived RGB omitted objective."""
    identities, configs = [], []
    for proof in proofs:
        config = proof['freeze']['config']
        identity = (config.get('objective', 'rgb'), config['seed'])
        if (proof['status'] != 'completed_local_route_pilot_not_noise_training'
                or config['model_kind'] != 'local' or config['steps'] != 2048):
            raise ValueError('Completed local final-2048 checkpoints required')
        identities.append(identity)
        configs.append({k: v for k, v in config.items() if k not in ('objective', 'seed')})
    objectives = {o for o, _ in identities}
    if (objectives not in ({'rgb', 'relative'}, {'rgb', 'relative', 'tempered'})
            or len(identities) != 2*len(objectives)
            or set(identities) != {(o, s) for o in objectives for s in (71, 73)}):
        raise ValueError('Each declared objective must have exactly seeds71/73, including RGB and relative')
    if any(c != configs[0] for c in configs[1:]):
        raise ValueError('Paired numerical configuration differs')
    return identities


def freeze_cases(source, prior, edge_start=160, mixed_seed=3200000):
    """New contents, not training or previously measured procedural truths."""
    if not isinstance(edge_start, int) or edge_start < 0 or not isinstance(mixed_seed, int) or mixed_seed < 0:
        raise ValueError('Nonnegative integer seed blocks required')
    edges = [freeze_edge(edge_recipe(i, 'reserved'), 128) for i in range(edge_start, edge_start+32)]
    mixed = [dict(family=f, seed=mixed_seed+100*i+j, original_phase_index=j)
        for i, f in enumerate(FAMILIES) for j in range(4)]
    seen = {c['target_sha256'] for c in source['freeze']['train_edge_events']}
    seen.update(c['target_sha256'] for c in source['freeze'].get('reserved_edges', []))
    seen.update(c['target_sha256'] for c in prior['edges']+prior['mixed'])
    for case in source['freeze']['train_slots']+source['freeze']['evaluation_cases']:
        seen.add(tensor_hash(signal(case['family'], case['seed'], 128)[None]))
    for case in mixed:
        truth = signal(case['family'], case['seed'], 128)[None]
        case['target_sha256'] = tensor_hash(truth)
        case['packed_sha256_by_phase'] = [tensor_hash(sample_rgb(truth, torch.tensor([p]))) for p in PATTERNS]
    hashes = [c['target_sha256'] for c in edges+mixed]
    if len(set(hashes)) != 52 or seen.intersection(hashes):
        raise ValueError('New truth collision with training or earlier controls')
    return edges, mixed


def confined(root, paths):
    base = (root/'build-patch_neural_restoration').resolve()
    if not all(p.resolve().is_relative_to(base) for p in paths):
        raise ValueError('Use dedicated neural build')


def prepare(root, source, priors, models, output, edge_start=160, mixed_seed=3200000):
    confined(root, [source, output]+priors+models)
    proofs = [json.loads((m/'result.json').read_text(encoding='utf-8')) for m in models]
    identities = model_contract(proofs)
    data = json.loads((source/'result.json').read_text(encoding='utf-8'))
    if data['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Completed frozen training source required')
    source_hash = digest(source/'result.json')
    for m, p in zip(models, proofs):
        if (p['freeze']['source_result_sha256'] != source_hash
                or p['freeze']['gates'] != data['freeze']['gates']
                or digest(m/'weights.pt') != p['weights_sha256']):
            raise ValueError('Checkpoint/source/gate identity differs')
        if any(p['freeze']['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in MODEL_TOOLS):
            raise ValueError('Model or data implementation differs')
    guard = MemoryGuard()
    with guard:
        torch.set_num_threads(4)
        prior_data = [json.loads(p.read_text(encoding='utf-8')) for p in priors]
        seen = {key: [r for p in prior_data for r in p[key]] for key in ('edges', 'mixed')}
        edges, mixed = freeze_cases(data, seen, edge_start, mixed_seed)
        guard.check()
    plan = dict(schema_version=1, frozen_at_utc=datetime.now(timezone.utc).isoformat(),
        source=str(source.resolve()), source_result_sha256=source_hash,
        prior_plan_sha256=[digest(p) for p in priors], gates=data['freeze']['gates'], edges=edges, mixed=mixed,
        models=[dict(folder=str(m.resolve()), objective=o, seed=s,
            result_sha256=digest(m/'result.json'), weights_sha256=p['weights_sha256'])
            for m, p, (o, s) in zip(models, proofs, identities)],
        tools={n: digest(Path(__file__).with_name(n)) for n in TOOLS},
        preparation_memory=guard.stats(),
        policy='Frozen after all four fits, before any prediction on these truths; no retraining, best-seed selection or gate relaxation. Same procedural generators, not independent photos.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def paired(records):
    """Aggregate and case counts versus the same-seed RGB witness, no selection."""
    answers = []
    for seed, objective in ((s, o) for s in (71, 73) for o in sorted({r['objective'] for r in records}-{'rgb'})):
        before = next(r for r in records if r['seed'] == seed and r['objective'] == 'rgb')
        after = next(r for r in records if r['seed'] == seed and r['objective'] == objective)
        for group in ('mixed_records', 'edge_records'):
            a, b = before[group], after[group]
            key = lambda r: (r['family'], r['seed'], r['phase_index'], r['target_sha256'], r['packed_sha256'])
            if [key(r) for r in a] != [key(r) for r in b]:
                raise ValueError('Paired evaluation identities differ')
            for family in ('all',)+tuple(sorted({r['family'] for r in a})):
                pairs = [(x, y) for x, y in zip(a, b) if family == 'all' or x['family'] == family]
                old = aggregate([x for x, _ in pairs])
                new = aggregate([y for _, y in pairs])
                deltas = [(y['metrics']['output_sse']-x['metrics']['output_sse'])/(3*x['metrics']['pixels']) for x, y in pairs]
                answers.append(dict(seed=seed, candidate_objective=objective, group=group, family=family, cases=len(pairs),
                    rgb_objective=old, candidate=new,
                    output_mse_ratio_candidate_to_rgb=new['output_mse']/old['output_mse'] if old['output_mse'] > 1e-12 else None,
                    better=sum(d < -1e-12 for d in deltas), worse=sum(d > 1e-12 for d in deltas),
                    tied_within_1e_12=sum(abs(d) <= 1e-12 for d in deltas)))
    return answers


def run(root, plan_path, output):
    confined(root, [plan_path, output])
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
        raise ValueError('Frozen implementation changed')
    confined(root, [Path(m['folder']) for m in plan['models']]+[Path(plan['source'])])
    if digest(Path(plan['source'])/'result.json') != plan['source_result_sha256']:
        raise ValueError('Frozen source changed')
    now = datetime.now(timezone.utc)
    campaign = 'neural-objective-confirmation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        plan=plan, plan_sha256=digest(plan_path), models=[], status='incomplete',
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        limits=['Procedural noise-free confirmation only; no natural scene or sensor qualification.',
            'Both seeds and all cases reported, no learned blend or checkpoint selection.',
            'No superiority criterion fitted to results; absolute gates versus bilinear retained.',
            'Original20 here means designated phases of new mixed contents, not historical truths.'])
    guard, started = MemoryGuard(), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for item in plan['models']:
                guard.check()
                folder = Path(item['folder'])
                if digest(folder/'result.json') != item['result_sha256'] or digest(folder/'weights.pt') != item['weights_sha256']:
                    raise ValueError('Frozen model changed')
                proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
                checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
                if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
                    raise ValueError('Checkpoint provenance differs')
                model = LocalMomentUNet(proof['freeze']['config']['width']).eval()
                model.load_state_dict(checkpoint['state_dict'], strict=True)
                model.requires_grad_(False)
                prepared = prepare_inference(model)
                rows, edges, selected = [], [], []
                scale = proof['freeze']['config']['scale']
                with torch.inference_mode():
                    for case in plan['mixed']+plan['edges']:
                        is_edge = 'family' not in case
                        truth = edge_image(case) if is_edge else signal(case['family'], case['seed'], 128)[None]
                        if tensor_hash(truth) != case['target_sha256']:
                            raise ValueError('Frozen truth differs')
                        for phase, p in enumerate(PATTERNS):
                            guard.check()
                            tags = torch.tensor([p])
                            packed = sample_rgb(truth, tags)
                            packed_hash = tensor_hash(packed)
                            if packed_hash != case['packed_sha256_by_phase'][phase]:
                                raise ValueError('Frozen observation differs')
                            args = (packed/scale, torch.zeros_like(packed), tags, True, True)
                            image = predict(prepared, *args)*scale
                            original = predict(model, *args)*scale
                            if not torch.equal(image, original) or not torch.equal(sample_rgb(image, tags), packed):
                                raise ValueError('Prepared or observed-site identity differs')
                            row = dict(family='edge' if is_edge else case['family'], seed=case['seed'], phase_index=phase,
                                target_sha256=case['target_sha256'], packed_sha256=packed_hash,
                                output_sha256=tensor_hash(image), observed_site_mse=0.,
                                metrics=metrics(truth, bilinear(packed, tags), image))
                            (edges if is_edge else rows).append(row)
                            if not is_edge and phase == case['original_phase_index']:
                                selected.append(row)
                if any(not torch.equal(v, model.state_dict()[n]) for n, v in checkpoint['state_dict'].items()):
                    raise ValueError('Source model changed')
                result['models'].append(dict(seed=item['seed'], objective=item['objective'],
                    mixed_records=rows, edge_records=edges, mixed=summarize(rows, plan['gates']),
                    original20=summarize(selected, plan['gates']), edges=aggregate(edges), prepared_bit_exact_cases=len(rows)+len(edges)))
                print('MEASURED '+item['objective']+' '+str(item['seed']), flush=True)
                del model, prepared, checkpoint
            result['paired'] = paired(result['models'])
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
                raise ValueError('Frozen implementation changed during evaluation')
            result['status'] = 'completed_paired_objective_confirmation_no_noise_training'
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
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--prior', nargs='+', type=Path)
    parser.add_argument('--models', nargs='+', type=Path)
    parser.add_argument('--edge-start', type=int, default=160)
    parser.add_argument('--mixed-seed', type=int, default=3200000)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        if args.source is None or args.prior is None or args.models is None:
            parser.error('Preparation requires source, prior plans and paired models')
        prepare(root, args.source, args.prior, args.models, args.output, args.edge_start, args.mixed_seed)
    elif args.plan is None:
        parser.error('Evaluation requires frozen plan')
    else:
        run(root, args.plan, args.output)
