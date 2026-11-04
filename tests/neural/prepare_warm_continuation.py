"""Freeze a paired512-step continuation and bounded warm-start preflight."""
import argparse
import copy
from datetime import datetime, timezone
import json
from pathlib import Path

import torch

from continuation_initialization import initialize_continuation
from diagnose_train_learnability import digest, save
from memory_budget import MemoryGuard
from noisy_moment_unet import NoisyMomentUNet, FORWARD_TOOLS
from noisy_training_policy import configure_trainable
from run_balanced_moment_pilot import balanced_schedule
from run_noisy_moment_pilot import train, TOOLS, MEMORY_POLICY
from run_synthetic_joint_pilot import tensor_hash


def prefix_source(source, steps):
    result = copy.deepcopy(source)
    schedule, _ = balanced_schedule(source['freeze']['train_slots'], steps, 4, 128)
    result.update(status='frozen_continuation_source_not_qualification',
        derivation='Replay the first declared training exposures; historical evaluation targets unchanged.')
    result['freeze']['config']['steps'] = steps
    result['freeze']['phase_schedule_sha256'] = tensor_hash(torch.tensor([[*r['ids'], *r['phases']] for r in schedule]))
    result['freeze']['train_edge_events'] = [e for e in source['freeze']['train_edge_events'] if e['step'] <= steps]
    return result


def prepare(root, output):
    base = root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()):
        raise ValueError('Dedicated neural build required')
    original_path = base/'noisy-structure-plan-v001/plan.json'
    original = json.loads(original_path.read_text(encoding='utf-8'))
    source_path = Path(original['source'])/'result.json'
    if digest(source_path) != original['source_result_sha256']:
        raise ValueError('Original training source changed')
    source = json.loads(source_path.read_text(encoding='utf-8'))
    for name in FORWARD_TOOLS:
        if digest(Path(__file__).with_name(name)) != original['tools'][name]:
            raise ValueError('Initializer forward arithmetic changed')
    refs = []
    for seed in (71, 73):
        folder = base/f'noisy-structure-campaign-v001/seed{seed}-attempt01'
        proof_path, weights = folder/'result.json', folder/'weights.pt'
        proof = json.loads(proof_path.read_text(encoding='utf-8'))
        if (proof['plan_sha256'] != digest(original_path) or proof['seed'] != seed
                or proof['training_progress']['completed_optimizer_steps'] != 2048
                or proof['pilot_gates_passed'] is not True or proof['weights_sha256'] != digest(weights)):
            raise ValueError('Both declared learned initializers required')
        refs.append(dict(seed=seed, folder=str(folder.resolve()), result_sha256=digest(proof_path),
            weights_sha256=digest(weights)))
    output.mkdir(parents=True, exist_ok=False)
    now = datetime.now(timezone.utc)
    identity = 'neural-warm-preflight-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    preflight = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', source_plan_sha256=digest(original_path), script_sha256=digest(Path(__file__)),
        models=refs, cases=[], policy='Eight engineering steps per seed/arm; fresh Adam, rate0.00025, transition compressed to4steps only for engineering. Discard weights, no quality decision.')
    guard = MemoryGuard(**MEMORY_POLICY)
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            engineering_source = prefix_source(source, 8)
            engineering_events = [e for e in original['texture_augmentation']['events'] if e['serial'] < 32]
            for arm, end in (('control', 0.), ('progressive', 1.)):
                for ref in refs:
                    guard.check()
                    torch.manual_seed(ref['seed'])
                    parent_ref = next(r for r in original['parents'] if r['seed'] == ref['seed'])
                    clean_state = torch.load(Path(parent_ref['folder'])/'weights.pt', weights_only=True)['state_dict']
                    if digest(Path(parent_ref['folder'])/'weights.pt') != parent_ref['weights_sha256']:
                        raise ValueError('Frozen reconstruction changed')
                    model = NoisyMomentUNet(8)
                    initialize_continuation(root, model, clean_state, ref)
                    configure_trainable(model, 'cfa_front')
                    config = dict(original['config'], steps=8, learning_rate=.00025,
                        structure_schedule=dict(start=0., end=end, ramp_steps=4))
                    context = dict(plan_sha256='engineering-'+digest(original_path), seed=ref['seed'], config=config,
                        parent_weights_sha256=ref['weights_sha256'], source_result_sha256=digest(source_path))
                    progress = {}
                    losses, counts = train(model, engineering_source, config, guard,
                        output/f'engineering-{arm}-{ref["seed"]}.pt', context, progress, 8,
                        texture_events=engineering_events)
                    if any(not torch.equal(v, model.state_dict()[n]) for n,v in clean_state.items()):
                        raise ValueError('Continuation changed frozen reconstruction')
                    preflight['cases'].append(dict(arm=arm, seed=ref['seed'], completed_steps=len(losses),
                        exposures=sum(counts.values()), final_loss=losses[-1], progress=progress))
                    print('ENGINEERING', arm, ref['seed'], flush=True)
                    del model, clean_state
            preflight['status'] = 'passed_paired_warm_engineering_not_quality_qualification'
    finally:
        preflight['memory'] = guard.stats()
        save(output/'preflight.json', preflight)
        save(root/'docs/validation/raw'/f'{identity}.json', preflight)
    source512 = prefix_source(source, 512)
    source512['origin'] = dict(path=str(source_path), sha256=digest(source_path))
    source_folder = output/'source512'
    source_folder.mkdir()
    save(source_folder/'result.json', source512)
    tools = {n:digest(Path(__file__).with_name(n)) for n in TOOLS+('prepare_warm_continuation.py',)}
    for arm, end in (('control', 0.), ('progressive', 1.)):
        plan = copy.deepcopy(original)
        for previous_trial in ('supervision_trial', 'stability_trial'):
            plan.pop(previous_trial, None)
        plan.update(frozen_at_utc=datetime.now(timezone.utc).isoformat(), tools=tools,
            source=str(source_folder.resolve()), source_result_sha256=digest(source_folder/'result.json'),
            warm_start=dict(models=refs, completed_optimizer_steps=2048, optimizer='Fresh Adam in both arms; moments not inherited.'),
            engineering_preflight=dict(path=str((output/'preflight.json').resolve()),
                sha256=digest(output/'preflight.json'), campaign_id=identity),
            continuation=dict(arm=arm, initial_plan_sha256=digest(original_path), stage_steps=512,
                total_optimizer_steps=2560, data='Exact first512 original exposures, phases and noise draws replayed in both arms.'),
            structure_preservation=dict(weight=1., secondary_schedule=dict(start=0., end=end, ramp_steps=256),
                policy='Continuous convex mixture between single-projection and averaged dual-projection losses; one current implementation.'),
            training_policy='Final512-stage checkpoint only after2048inherited steps; both seeds and arms fixed before outcomes. Fresh Adam0.00025, same replay and gradient cap1; no score selection.')
        plan['config'].update(steps=512, learning_rate=.00025,
            structure_schedule=dict(start=0., end=end, ramp_steps=256))
        plan['texture_augmentation']['events'] = [e for e in original['texture_augmentation']['events'] if e['serial'] < 2048]
        folder = output/arm
        folder.mkdir()
        save(folder/'plan.json', plan)
    print('PREPARED', output, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[2], args.output)
