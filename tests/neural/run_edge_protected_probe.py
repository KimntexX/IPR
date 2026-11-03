"""One fixed protection probe on frozen learned corrections; no retraining."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from compare_reconstruction_objectives import confined
from diagnose_train_learnability import digest, save
from edge_protected_noise import EdgeProtectedNoisyUNet, SCORE_ONSET, NOMINAL_FAMILY_ALPHA, COMPARISON_COUNT, HALO_RADIUS
from local_moment_unet import LocalMomentUNet
from noisy_moment_unet import FORWARD_TOOLS
from memory_budget import MemoryGuard, BudgetExceeded
from run_noisy_moment_pilot import TOOLS, evaluate, variant_summary, runtime_versions


PROBE_TOOLS = tuple(dict.fromkeys(TOOLS+('run_edge_protected_probe.py', 'edge_protected_noise.py')))


def prepare(root, data_plan, folders, output):
    confined(root, [data_plan, output]+folders)
    source = json.loads(data_plan.read_text(encoding='utf-8'))
    if source['config']['training_scope'] != 'cfa_front':
        raise ValueError('Frozen reconstruction experiment required')
    references = []
    for folder in folders:
        proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
        if (proof['status'] != 'completed_synthetic_noise_pilot_not_photographic_qualification'
                or proof['plan_sha256'] != digest(data_plan)
                or proof['training_progress']['completed_optimizer_steps'] != 2048
                or digest(folder/'weights.pt') != proof['weights_sha256']):
            raise ValueError('Final complete frozen-trunk witness required, without score selection')
        if any(proof['plan']['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in FORWARD_TOOLS):
            raise ValueError('Trained model implementation differs; use its archived revision')
        references.append(dict(seed=proof['seed'], folder=str(folder.resolve()),
            result_sha256=digest(folder/'result.json'), weights_sha256=proof['weights_sha256']))
    if len(references) != 2 or {r['seed'] for r in references} != {71, 73}:
        raise ValueError('Both seeds required')
    plan = dict(schema_version=1, frozen_at_utc=datetime.now(timezone.utc).isoformat(),
        data_plan_path=str(data_plan.resolve()), data_plan_sha256=digest(data_plan), models=references,
        memory_policy=source['memory_policy'],
        policy='Fixed second-order contrast excluding centre, correct variance weights, nominal Gaussian union bound across local tests; no fitted score threshold or learned-weight change.',
        protection=dict(score_onset=SCORE_ONSET, nominal_family_alpha=NOMINAL_FAMILY_ALPHA,
            comparison_count=COMPARISON_COUNT, halo_radius=HALO_RADIUS),
        limits=['Consulted development controls; independent noisy confirmation remains required.',
            'Estimated Gaussian contrast scale is a heuristic, not a calibrated edge probability.',
            'Protection may reduce denoising and fail on weak structures; all original gates retained.'],
        tools={n: digest(Path(__file__).with_name(n)) for n in PROBE_TOOLS})
    output.mkdir(parents=True, exist_ok=False)
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def compare_records(current, prior):
    key = lambda r: (r['target_sha256'], r['noisy_sha256'], r['sigma_sha256'], r['phase_index'], r['noise_index'])
    if ([key(r) for r in current] != [key(r) for r in prior]
            or any(r['output_sha256']['parent'] != old['output_sha256']['parent'] for r, old in zip(current, prior))):
        raise ValueError('Observation or parent replay differs from unprotected witness')
    result = []
    for level in range(3):
        for group in ('mixed', 'distinct_edges', 'all'):
            indices = [i for i, r in enumerate(current) if r['noise_index'] == level and
                (group == 'all' or r['mixed'] == (group == 'mixed'))]
            new = variant_summary([current[i] for i in indices], 'full')
            old = variant_summary([prior[i] for i in indices], 'full')
            deltas = [(current[i]['metrics']['full']['output_sse']-prior[i]['metrics']['full']['output_sse'])/
                (3*current[i]['metrics']['full']['pixels']) for i in indices]
            result.append(dict(noise_index=level, group=group, protected=new, unprotected=old,
                mse_ratio_protected_over_unprotected=new['output_mse']/old['output_mse'] if old['output_mse'] > 1e-12 else None,
                better=sum(d < -1e-12 for d in deltas), worse=sum(d > 1e-12 for d in deltas),
                tied=sum(abs(d) <= 1e-12 for d in deltas)))
    return result


def run(root, plan_path, output):
    confined(root, [plan_path, output])
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
        raise ValueError('Frozen probe changed')
    data_path = Path(plan['data_plan_path'])
    confined(root, [data_path]+[Path(m['folder']) for m in plan['models']])
    if digest(data_path) != plan['data_plan_sha256']:
        raise ValueError('Data plan changed')
    data = json.loads(data_path.read_text(encoding='utf-8'))
    if data.get('runtime') != runtime_versions():
        raise ValueError('Probe runtime differs from qualified runtime')
    source_path = Path(data['source'])/'result.json'
    confined(root, [source_path])
    if digest(source_path) != data['source_result_sha256']:
        raise ValueError('Frozen source changed')
    source = json.loads(source_path.read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    campaign = 'neural-edge-protection-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        status='incomplete', plan=plan, plan_sha256=digest(plan_path), models=[],
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip())
    guard, started = MemoryGuard(**plan['memory_policy']), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for ref in plan['models']:
                guard.check()
                folder = Path(ref['folder'])
                if digest(folder/'result.json') != ref['result_sha256'] or digest(folder/'weights.pt') != ref['weights_sha256']:
                    raise ValueError('Frozen model changed')
                prior = json.loads((folder/'result.json').read_text(encoding='utf-8'))
                state = torch.load(folder/'weights.pt', weights_only=True, map_location='cpu')
                if state['plan_sha256'] != prior['plan_sha256']:
                    raise ValueError('Trained checkpoint provenance differs')
                model = EdgeProtectedNoisyUNet(data['config']['width']).eval().requires_grad_(False)
                model.load_state_dict(state['state_dict'], strict=True)
                parent_ref = next(p for p in data['parents'] if p['seed'] == ref['seed'])
                parent_path = Path(parent_ref['folder'])/'weights.pt'
                confined(root, [parent_path])
                if digest(parent_path) != parent_ref['weights_sha256']:
                    raise ValueError('Frozen parent changed')
                teacher_state = torch.load(parent_path, weights_only=True, map_location='cpu')
                parent = LocalMomentUNet(data['config']['width']).eval().requires_grad_(False)
                parent.load_state_dict(teacher_state['state_dict'], strict=True)
                measured = evaluate(model, parent, data, source, guard)
                measured['paired_protection'] = compare_records(measured['records'], prior['records'])
                measured.update(seed=ref['seed'], source_result_sha256=ref['result_sha256'],
                    learned_weights_sha256=ref['weights_sha256'], fitting_steps=0)
                if any(not torch.equal(v, model.state_dict()[n]) for n, v in state['state_dict'].items()):
                    raise ValueError('Learned weights changed')
                result['models'].append(measured)
                print('MEASURED '+str(ref['seed'])+' gates='+str(measured['pilot_gates_passed']), flush=True)
                del model, parent, state, teacher_state
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in plan['tools'].items()):
                raise ValueError('Frozen probe changed during evaluation')
            result['status'] = 'completed_fixed_edge_protection_probe_not_photographic_qualification'
            result['pilot_gates_passed'] = all(m['pilot_gates_passed'] for m in result['models'])
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
    parser.add_argument('--data-plan', type=Path)
    parser.add_argument('--models', nargs=2, type=Path)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        if args.data_plan is None or args.models is None:
            parser.error('Preparation requires data plan and both source models')
        prepare(root, args.data_plan, args.models, args.output)
    elif args.plan is None:
        parser.error('Evaluation requires frozen plan')
    else:
        run(root, args.plan, args.output)
