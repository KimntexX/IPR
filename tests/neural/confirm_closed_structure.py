"""Prepare untouched confirmation truths, then evaluate frozen independent seeds."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess

import torch

from closed_moment_unet import build_model
from diagnose_train_learnability import digest, save
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import measure, summaries
from run_diverse_moment_pilot import edge_recipe, freeze_edge, edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import FAMILIES, PATTERNS, signal


def prepare(root, output):
    if not output.resolve().is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Use dedicated neural build')
    torch.set_num_threads(4)
    edges = [freeze_edge(edge_recipe(i, 'reserved'), 128) for i in range(64, 96)]
    mixed = [dict(family=f, seed=9200+100*i+j, original_phase_index=j)
        for i, f in enumerate(FAMILIES) for j in range(4)]
    for case in mixed:
        truth = signal(case['family'], case['seed'], 128)[None]
        case['target_sha256'] = tensor_hash(truth)
    output.mkdir(parents=True, exist_ok=False)
    now = datetime.now(timezone.utc)
    plan = dict(schema_version=1, measured_at_utc=now.isoformat(), edges=edges, mixed=mixed,
        training_disjoint_seed_rule='Train edges1000000..1000417; held edges2000064..2000095; mixed9200+100family+j vs train7300/eval8400 series.',
        structure_policy='Equivariant architecture chosen for algebraic contract, not confirmation score; report both seeds71and73, no best-seed selection.',
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        tool_sha256=digest(Path(__file__)), policy='Truths frozen before confirmation inference; no training or optimizer use.')
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def run(root, plan_path, models, output):
    if len(models) != 2 or not all(p.resolve().is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in models+[output, plan_path]):
        raise ValueError('Two independent models and dedicated neural paths required')
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if digest(Path(__file__)) != plan['tool_sha256']:
        raise ValueError('Confirmation implementation changed after preparation')
    proofs = [json.loads((folder/'result.json').read_text(encoding='utf-8')) for folder in models]
    if {p['freeze']['config']['seed'] for p in proofs} != {71, 73} or any(
            p['status'] != 'completed_closed_unet_pilot_not_photographic_qualification'
            or p['freeze']['config']['model_kind'] != 'equivariant' for p in proofs):
        raise ValueError('Expected completed equivariant seeds71and73')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    now = datetime.now(timezone.utc)
    campaign = 'neural-structure-confirmation-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        plan=plan, plan_sha256=digest(plan_path), models=[], status='incomplete',
        limits=['New procedural truths share existing generator, not natural-scene or sensor-noise validation.',
            'No best seed, threshold tuning or model change based on confirmation.',
            'Old failed gates remain failures even if confirmation passes.'])
    try:
        for folder, proof in zip(models, proofs):
            config = proof['freeze']['config']
            if digest(folder/'weights.pt') != proof['weights_sha256']:
                raise ValueError('Model changed')
            model = build_model('equivariant', config['width'])
            checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
            if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
                raise ValueError('Checkpoint provenance differs')
            model.load_state_dict(checkpoint['state_dict'], strict=True)
            model.eval()
            model.requires_grad_(False)
            rows, original, edges = [], [], []
            with torch.inference_mode():
                for case in plan['mixed']:
                    truth = signal(case['family'], case['seed'], config['side'])[None]
                    if tensor_hash(truth) != case['target_sha256']:
                        raise ValueError('Confirmation mixed target changed')
                    for phase, p in enumerate(PATTERNS):
                        record = dict(family=case['family'], seed=case['seed'], phase_index=phase,
                            **measure(model, truth, torch.tensor([p]), config['scale']))
                        rows.append(record)
                        if phase == case['original_phase_index']:
                            original.append(record)
                for case in plan['edges']:
                    truth = edge_image(case, config['side'])
                    if tensor_hash(truth) != case['target_sha256']:
                        raise ValueError('Confirmation edge target changed')
                    for phase, p in enumerate(PATTERNS):
                        record = dict(family='edge', seed=case['seed'], phase_index=phase,
                            **measure(model, truth, torch.tensor([p]), config['scale']))
                        if record['packed_sha256'] != case['packed_sha256_by_phase'][phase]:
                            raise ValueError('Confirmation edge observation changed')
                        edges.append(record)
            score = summaries(rows)
            result['models'].append(dict(seed=config['seed'], source_result_sha256=digest(folder/'result.json'),
                weights_sha256=proof['weights_sha256'], mixed_records=rows, edge_records=edges,
                mixed=score, original20=summaries(original), edges=summaries(edges),
                mixed_qualification=qualifies(dict(records=rows, **score), proof['freeze']['gates']),
                original20_qualification=qualifies(dict(records=original, **summaries(original)), proof['freeze']['gates'])))
        result['status'] = 'completed_independent_seed_procedural_confirmation_no_training'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps([dict(seed=m['seed'], mixed=m['mixed']['summary'], edges=m['edges']['summary'],
        qualification=m['mixed_qualification']) for m in result['models']], indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--models', nargs=2, type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        prepare(root, args.output)
    elif args.plan is None or args.models is None:
        parser.error('Evaluation requires --plan and two --models')
    else:
        run(root, args.plan, args.models, args.output)
