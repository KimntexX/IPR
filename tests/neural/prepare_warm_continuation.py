"""Freeze a paired512-step continuation and bounded warm-start preflight."""
import argparse
import copy
from datetime import datetime, timezone
import json
from pathlib import Path

import torch

from continuation_initialization import initialize_continuation
from bayer_canonical import predict
from context_data import extend_case, extend_observation, crop_centre
from diagnose_train_learnability import digest, save
from memory_budget import MemoryGuard
from noisy_moment_unet import NoisyMomentUNet, FORWARD_TOOLS
from multiscale_correction import composed_support
from noisy_training_policy import configure_trainable
from run_balanced_moment_pilot import balanced_schedule
from run_noisy_moment_pilot import train, TOOLS, MEMORY_POLICY, observation, NOISE, runtime_versions
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def prefix_source(source, steps):
    schedule, counts = balanced_schedule(source['freeze']['train_slots'], steps, 4, 128)
    freeze = {key:copy.deepcopy(source['freeze'][key]) for key in ('config', 'train_slots', 'evaluation_cases')}
    freeze['config']['steps'] = steps
    freeze['phase_schedule_sha256'] = tensor_hash(torch.tensor([[*r['ids'], *r['phases']] for r in schedule]))
    freeze['phase_counts_by_slot'] = counts.tolist()
    freeze['train_edge_events'] = [copy.deepcopy(e) for e in source['freeze']['train_edge_events'] if e['step'] <= steps]
    result = dict(status='frozen_continuation_source_not_qualification', freeze=freeze,
        derivation='Replay declared prefix; phase counts recomputed; historical evaluation targets unchanged. No inherited training results or unused full-campaign counters.')
    return result


def check_embedding(model, original, ref, guard):
    proof = json.loads((Path(ref['folder'])/'result.json').read_text(encoding='utf-8'))
    lookup = {(r['case_index'], r['phase_index'], r['noise_index']):r for r in proof['records']}
    fields = {(r['case_index'], r['phase'], r['noise_index']):r for r in original['observations']}
    chosen, families = [], set()
    for index, case in enumerate(original['mixed']):
        if case['family'] not in families:
            chosen.append((index, case))
            families.add(case['family'])
    checks = []
    model.eval()
    with torch.inference_mode():
        for index, case in chosen:
            truth = extend_case(case, original['context_policy']['validation_side'])
            centre = crop_centre(truth)
            for phase, pattern in enumerate(PATTERNS):
                tags = torch.tensor([pattern])
                for level in (1, 2):
                    guard.check()
                    field = fields[index, phase, level]
                    values, sigma = observation(centre, tags, level, field['noise_seed'])
                    if tensor_hash(values) != field['noisy_sha256'] or tensor_hash(sigma) != field['sigma_sha256']:
                        raise ValueError('Initializer bridge changed original observations')
                    values, sigma = extend_observation(truth, tags, values, sigma, *NOISE[level],
                        original['context_policy']['validation_halo_seed_base']+field['noise_seed'])
                    output = crop_centre(predict(model, values/2, sigma/2, tags, False, True)*2)
                    expected = lookup[index, phase, level]['output_sha256']['full']
                    if tensor_hash(output) != expected:
                        raise ValueError('Zero statistics projection did not preserve archived forward exactly')
                    checks.append(dict(seed=ref['seed'], case_index=index, phase=phase, noise_index=level,
                        bit_exact=True, output_sha256=expected))
    model.train()
    return checks


def prepare(root, output, engineering_path):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (output, engineering_path)):
        raise ValueError('Dedicated neural build required')
    original_path = base/'noisy-structure-plan-v001/plan.json'
    original = json.loads(original_path.read_text(encoding='utf-8'))
    source_path = Path(original['source'])/'result.json'
    if digest(source_path) != original['source_result_sha256']:
        raise ValueError('Original training source changed')
    source = json.loads(source_path.read_text(encoding='utf-8'))
    engineering = json.loads(engineering_path.read_text(encoding='utf-8'))
    if (engineering['status'] != 'passed_multiscale_engineering_preflight_not_quality_qualification'
            or engineering['support'] != composed_support() or engineering['runtime'] != runtime_versions()
            or any(digest(Path(__file__).with_name(n)) != engineering['tools'][n] for n in FORWARD_TOOLS)):
        raise ValueError('Current correlation architecture engineering required')
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
            weights_sha256=digest(weights), initializer_embedding='zero_correlation_projection_v1'))
    output.mkdir(parents=True, exist_ok=False)
    now = datetime.now(timezone.utc)
    identity = 'neural-statistics-warm-preflight-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    preflight = dict(schema_version=1, data_version=1, campaign_id=identity, measured_at_utc=now.isoformat(),
        status='incomplete', source_plan_sha256=digest(original_path), script_sha256=digest(Path(__file__)),
        models=refs, cases=[], embedding_checks=[], context_engineering_sha256=digest(engineering_path),
        policy='Eight engineering steps per seed/arm; source-forward SHA bridge before any update. Fresh Adam0.00025, single-projection loss in both arms. Statistics frozen-zero control vs learned. Discard engineering weights.')
    guard = MemoryGuard(**MEMORY_POLICY)
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            exclusions = {e['target_sha256'] for e in original['texture_augmentation']['events']}
            exclusions.update(e['target_sha256'] for e in source['freeze']['train_edge_events'])
            exclusions.update(e['target_sha256'] for e in source['freeze'].get('reserved_edges', []))
            for case in source['freeze']['train_slots']+source['freeze']['evaluation_cases']:
                guard.check()
                exclusions.add(tensor_hash(signal(case['family'], case['seed'], 128)[None]))
            exclusions.update(original.get('lineage_exclusions', {}).get('target_sha256', []))
            preflight['ancestor_truths_excluded'] = len(exclusions)
            engineering_source = prefix_source(source, 8)
            engineering_events = [e for e in original['texture_augmentation']['events'] if e['serial'] < 32]
            for arm, enabled in (('control', False), ('correlation', True)):
                for ref in refs:
                    guard.check()
                    torch.manual_seed(ref['seed'])
                    parent_ref = next(r for r in original['parents'] if r['seed'] == ref['seed'])
                    clean_state = torch.load(Path(parent_ref['folder'])/'weights.pt', weights_only=True)['state_dict']
                    if digest(Path(parent_ref['folder'])/'weights.pt') != parent_ref['weights_sha256']:
                        raise ValueError('Frozen reconstruction changed')
                    model = NoisyMomentUNet(8)
                    initialize_continuation(root, model, clean_state, ref)
                    configure_trainable(model, 'cfa_front', enabled)
                    if arm == 'control':
                        preflight['embedding_checks'].extend(check_embedding(model, original, ref, guard))
                    config = dict(original['config'], steps=8, learning_rate=.00025,
                        structure_schedule=dict(start=0., end=0., ramp_steps=4), statistics_trainable=enabled)
                    context = dict(plan_sha256='engineering-'+digest(original_path), seed=ref['seed'], config=config,
                        parent_weights_sha256=ref['weights_sha256'], source_result_sha256=digest(source_path))
                    progress = {}
                    losses, counts = train(model, engineering_source, config, guard,
                        output/f'engineering-{arm}-{ref["seed"]}.pt', context, progress, 8,
                        texture_events=engineering_events)
                    if any(not torch.equal(v, model.state_dict()[n]) for n,v in clean_state.items()):
                        raise ValueError('Continuation changed frozen reconstruction')
                    magnitude = float(model.noise_features.statistics.weight.detach().norm())
                    if (enabled and magnitude == 0.) or (not enabled and magnitude != 0.):
                        raise ValueError('Declared statistics trainability not exercised')
                    preflight['cases'].append(dict(arm=arm, seed=ref['seed'], completed_steps=len(losses),
                        exposures=sum(counts.values()), final_loss=losses[-1], progress=progress,
                        statistics_weight_norm=magnitude))
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
    for arm, enabled in (('control', False), ('correlation', True)):
        plan = copy.deepcopy(original)
        for previous_trial in ('supervision_trial', 'stability_trial'):
            plan.pop(previous_trial, None)
        plan.update(frozen_at_utc=datetime.now(timezone.utc).isoformat(), tools=tools,
            lineage_exclusions=dict(target_sha256=sorted(exclusions), source_result_sha256=digest(source_path),
                initializer_plan_sha256=digest(original_path),
                scope='All original initializer texture events, full source training/reserved edges and fixed source train/evaluation slots, not only the active512-step prefix.'),
            source=str(source_folder.resolve()), source_result_sha256=digest(source_folder/'result.json'),
            warm_start=dict(models=refs, completed_optimizer_steps=2048, optimizer='Fresh Adam in both arms; moments not inherited.'),
            engineering_preflight=dict(path=str((output/'preflight.json').resolve()),
                sha256=digest(output/'preflight.json'), campaign_id=identity),
            continuation=dict(arm=arm, experiment='observed_cfa_correlation_inputs', initial_plan_sha256=digest(original_path), stage_steps=512,
                total_optimizer_steps=2560, data='Exact first512 original exposures, phases and noise draws replayed in both arms.'),
            structure_preservation=dict(weight=1., secondary_schedule=dict(start=0., end=0., ramp_steps=256),
                policy='Single-projection structure supervision in both arms; only statistics projection trainability differs.'),
            conditioning='Original40 features plus32 dimensionless CFA correlations (9/17 windows), asinh compression, zero1x1 projection after first encoder block.',
            training_policy='Final512-stage checkpoint only after2048inherited steps; both seeds and arms fixed before outcomes. Fresh Adam0.00025, same replay/loss/gradient cap1. Frozen-zero statistics control vs learned statistics projection; no score selection.')
        plan['config'].update(steps=512, learning_rate=.00025,
            structure_schedule=dict(start=0., end=0., ramp_steps=256), statistics_trainable=enabled)
        plan['context_policy']['support'] = composed_support()
        plan['texture_augmentation']['events'] = [e for e in original['texture_augmentation']['events'] if e['serial'] < 2048]
        folder = output/arm
        folder.mkdir()
        save(folder/'plan.json', plan)
    print('PREPARED', output, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--engineering', type=Path, required=True)
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[2], args.output, args.engineering)
