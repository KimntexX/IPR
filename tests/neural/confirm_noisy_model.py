"""Eligibility-gated new-content confirmation, three fixed noise realizations."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import time

import torch

from compare_reconstruction_objectives import confined, freeze_cases
from continuation_policy import completed_training
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from noisy_moment_unet import NoisyMomentUNet, FORWARD_TOOLS
from run_edge_protected_probe import PROBE_TOOLS
from run_noisy_moment_pilot import evaluate, observation, runtime_versions
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


TOOLS = tuple(dict.fromkeys(PROBE_TOOLS+('confirm_noisy_model.py',)))


def eligibility(kind, proof, refs):
    if kind not in ('plain', 'protected') or proof.get('pilot_gates_passed') is not True:
        raise ValueError('Both initialization witnesses must pass before new-content inference')
    if {r['seed'] for r in refs} != {71, 73} or len(refs) != 2:
        raise ValueError('Both seeds required, no best-seed selection')
    if kind == 'plain':
        if proof['status'] != 'completed_both_seeds':
            raise ValueError('Completed two-seed campaign required')
        rows = [r for r in proof['attempts'] if r['status'] == 'completed_synthetic_noise_pilot_not_photographic_qualification']
        if len(rows) != 2 or {r['seed'] for r in rows} != {71, 73}:
            raise ValueError('Exactly both final results required')
        for ref in refs:
            row = next(r for r in rows if r['seed'] == ref['seed'])
            if row['result_sha256'] != ref['result_sha256'] or row['pilot_gates_passed'] is not True:
                raise ValueError('Unqualified or different final result')
    else:
        if proof['status'] != 'completed_fixed_edge_protection_probe_not_photographic_qualification':
            raise ValueError('Complete protected probe required')
        rows = proof['models']
        if len(rows) != 2 or {r['seed'] for r in rows} != {71, 73}:
            raise ValueError('Both protected results required')
        for ref in refs:
            row = next(r for r in rows if r['seed'] == ref['seed'])
            if (row['source_result_sha256'] != ref['result_sha256']
                    or row['learned_weights_sha256'] != ref['weights_sha256'] or row['pilot_gates_passed'] is not True):
                raise ValueError('Protected result differs from qualified weights')


def manifest_hash(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def cohort_contents(prior):
    content = prior.get('data', prior)
    if not all(isinstance(content.get(k), list) for k in ('edges', 'mixed')):
        raise ValueError('Previous cohort must declare edges and mixed contents')
    return content


def prepare(root, data_plan, accepted, kind, folders, priors, output, edge_start=288, mixed_seed=3600000):
    confined(root, [data_plan, accepted, output]+folders+priors)
    source_data = json.loads(data_plan.read_text(encoding='utf-8'))
    if source_data['config']['training_scope'] != 'cfa_front':
        raise ValueError('Frozen reconstruction contract required')
    if source_data.get('runtime') != runtime_versions():
        raise ValueError('Confirmation runtime differs')
    refs = []
    for folder in folders:
        proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
        if (proof['status'] != 'completed_synthetic_noise_pilot_not_photographic_qualification'
                or proof['plan_sha256'] != digest(data_plan)
                or digest(folder/'weights.pt') != proof['weights_sha256']
                or not completed_training(proof)
                or any(proof['plan']['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in FORWARD_TOOLS)):
            raise ValueError('Final fixed weights with current model required')
        refs.append(dict(seed=proof['seed'], folder=str(folder.resolve()),
            result_sha256=digest(folder/'result.json'), weights_sha256=proof['weights_sha256']))
    accepted_proof = json.loads(accepted.read_text(encoding='utf-8'))
    eligibility(kind, accepted_proof, refs)
    if kind == 'protected' and accepted_proof['plan']['tools']['edge_protected_noise.py'] != digest(Path(__file__).with_name('edge_protected_noise.py')):
        raise ValueError('Protection changed since eligibility')
    source_path = Path(source_data['source'])/'result.json'
    confined(root, [source_path])
    if digest(source_path) != source_data['source_result_sha256']:
        raise ValueError('Frozen training source changed')
    source = json.loads(source_path.read_text(encoding='utf-8'))
    guard = MemoryGuard(**source_data['memory_policy'])
    with guard:
        torch.set_num_threads(4)
        previous = [cohort_contents(json.loads(p.read_text(encoding='utf-8'))) for p in priors]+[source_data]
        seen = {key: [r for p in previous for r in p[key]] for key in ('edges', 'mixed')}
        seen['mixed'] += source_data.get('texture_augmentation', {}).get('events', [])
        edges, mixed = freeze_cases(source, seen, edge_start, mixed_seed)
        replicas = []
        for replicate in range(3):
            records = []
            for index, case in enumerate(mixed+edges):
                guard.check()
                truth = signal(case['family'], case['seed'], 128)[None] if 'family' in case else edge_image(case)
                for phase, p in enumerate(PATTERNS):
                    tags = torch.tensor([p])
                    for level in range(3):
                        seed = 96000000+replicate*1000000+index*1000+phase*10+level
                        packed, sigma = observation(truth, tags, level, seed)
                        records.append(dict(case_index=index, phase=phase, noise_index=level, noise_seed=seed,
                            noisy_sha256=tensor_hash(packed), sigma_sha256=tensor_hash(sigma)))
            replicas.append(dict(index=replicate, observations=records, manifest_sha256=manifest_hash(records)))
        data = dict(source_data, edges=edges, mixed=mixed)
        data.pop('observations')
        plan = dict(schema_version=1, frozen_at_utc=datetime.now(timezone.utc).isoformat(), kind=kind, data=data,
            role='independent',
            cohort_seed_block=dict(edge_start=edge_start, mixed_seed=mixed_seed),
            source_data_plan_sha256=digest(data_plan), eligibility_path=str(accepted.resolve()), eligibility_sha256=digest(accepted),
            models=refs, replicas=replicas, prior_plan_sha256=[digest(p) for p in priors],
            tools={n: digest(Path(__file__).with_name(n)) for n in TOOLS},
            policy='One new52-truth cohort, three fixed noise draws per positive case, both models; no refitting, seed selection or threshold changes.',
            limits=['Same procedural generators; not photographic, sensor or unseen noise-level validation.',
                'Zero-noise observations and historical controls replayed across replicas; not independent measurements.',
                'Three positive noise draws are independent across replicas, paired identically across model seeds.'])
        output.mkdir(parents=True, exist_ok=False)
    plan['preparation_memory'] = guard.stats()
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def reused_groups(prior, plan, plan_hash):
    if prior['status'] != 'aborted_for_memory_budget' or prior['plan_sha256'] != plan_hash:
        raise ValueError('Only exact-plan memory stops can be resumed')
    expected = {(m['seed'], r['index']): (m['weights_sha256'], r['manifest_sha256'])
        for m in plan['models'] for r in plan['replicas']}
    seen = set()
    cases = plan['data']['mixed']+plan['data']['edges']
    for group in prior['groups']:
        key = (group['seed'], group['replicate'])
        if (key in seen or key not in expected
                or (group['weights_sha256'], group['manifest_sha256']) != expected[key]
                or len(group['evaluation']['records']) != 624):
            raise ValueError('Saved evaluation group differs or duplicates')
        fields = next(r['observations'] for r in plan['replicas'] if r['index'] == group['replicate'])
        expected_fields = {(f['case_index'], f['phase'], f['noise_index']): f for f in fields}
        identities = set()
        for row in group['evaluation']['records']:
            identity = (row['case_index'], row['phase_index'], row['noise_index'])
            if identity in identities or identity not in expected_fields:
                raise ValueError('Saved observation identity differs')
            field = expected_fields[identity]
            if (row['noisy_sha256'] != field['noisy_sha256'] or row['sigma_sha256'] != field['sigma_sha256']
                    or row['target_sha256'] != cases[row['case_index']]['target_sha256']):
                raise ValueError('Saved observation content differs')
            identities.add(identity)
        seen.add(key)
    return prior['groups']


def run(root, plan_path, output, reuse=None):
    confined(root, [plan_path, output]+([reuse] if reuse else []))
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if plan.get('role', 'independent') not in ('independent', 'regression'):
        raise ValueError('Declared independent or regression evaluation role required')
    if plan['kind'] not in ('plain', 'protected'):
        raise ValueError('Declared model kind required')
    if plan['data'].get('runtime') != runtime_versions():
        raise ValueError('Confirmation runtime differs from frozen plan')
    if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
        raise ValueError('Frozen confirmation changed')
    groups = reused_groups(json.loads(reuse.read_text(encoding='utf-8')), plan, digest(plan_path)) if reuse else []
    source_path = Path(plan['data']['source'])/'result.json'
    confined(root, [source_path]+[Path(m['folder']) for m in plan['models']])
    if digest(source_path) != plan['data']['source_result_sha256']:
        raise ValueError('Training source changed')
    source = json.loads(source_path.read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    campaign = ('neural-noisy-confirmation-v001-' if plan.get('role', 'independent') == 'independent'
        else 'neural-noisy-regression-v001-')+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        plan=plan, plan_sha256=digest(plan_path), groups=groups, reused_groups=len(groups), status='incomplete',
        reused_from=dict(path=str(reuse.resolve()), sha256=digest(reuse)) if reuse else None,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip())
    guard, started = MemoryGuard(**plan['data']['memory_policy']), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in plan['models']:
                guard.check()
                folder = Path(ref['folder'])
                if digest(folder/'result.json') != ref['result_sha256'] or digest(folder/'weights.pt') != ref['weights_sha256']:
                    raise ValueError('Final trained model changed')
                weights = torch.load(folder/'weights.pt', weights_only=True, map_location='cpu')
                factory = EdgeProtectedNoisyUNet if plan['kind'] == 'protected' else NoisyMomentUNet
                model = factory(plan['data']['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(weights['state_dict'], strict=True)
                p = next(r for r in plan['data']['parents'] if r['seed'] == ref['seed'])
                parent_path = Path(p['folder'])/'weights.pt'
                confined(root, [parent_path])
                if digest(parent_path) != p['weights_sha256']:
                    raise ValueError('Parent changed')
                parent = LocalMomentUNet(plan['data']['config']['width']).eval().requires_grad_(False)
                parent.load_state_dict(torch.load(parent_path, weights_only=True, map_location='cpu')['state_dict'], strict=True)
                for replica in plan['replicas']:
                    if any(g['seed'] == ref['seed'] and g['replicate'] == replica['index'] for g in groups):
                        continue
                    if manifest_hash(replica['observations']) != replica['manifest_sha256']:
                        raise ValueError('Noisy validation manifest changed')
                    evaluated = evaluate(model, parent, dict(plan['data'], observations=replica['observations']), source, guard)
                    groups.append(dict(seed=ref['seed'], replicate=replica['index'],
                        weights_sha256=ref['weights_sha256'], manifest_sha256=replica['manifest_sha256'], evaluation=evaluated))
                    print('MEASURED seed'+str(ref['seed'])+' realization'+str(replica['index'])+' gates='+str(evaluated['pilot_gates_passed']), flush=True)
                if any(not torch.equal(v, model.state_dict()[n]) for n, v in weights['state_dict'].items()):
                    raise ValueError('Trained weights changed')
                del model, parent, weights
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
                raise ValueError('Confirmation changed during evaluation')
            result['status'] = ('completed_independent_noisy_confirmation_not_photographic_qualification'
                if plan.get('role', 'independent') == 'independent' else 'completed_noisy_regression_not_photographic_qualification')
            result['pilot_gates_passed'] = len(groups) == 6 and all(g['evaluation']['pilot_gates_passed'] for g in groups)
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
    print('COMPLETE '+campaign+' gates='+str(result['pilot_gates_passed']), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--data-plan', type=Path)
    parser.add_argument('--eligibility', type=Path)
    parser.add_argument('--kind', choices=('plain', 'protected'))
    parser.add_argument('--models', nargs=2, type=Path)
    parser.add_argument('--prior', nargs='+', type=Path)
    parser.add_argument('--edge-start', type=int, default=288)
    parser.add_argument('--mixed-seed', type=int, default=3600000)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--reuse', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        if args.data_plan is None or args.eligibility is None or args.kind is None or args.models is None or args.prior is None:
            parser.error('Preparation requires data, eligible proof, kind, both models and previous cohorts')
        prepare(root, args.data_plan, args.eligibility, args.kind, args.models, args.prior, args.output,
            args.edge_start, args.mixed_seed)
    elif args.plan is None:
        parser.error('Frozen plan required')
    else:
        run(root, args.plan, args.output, args.reuse)
