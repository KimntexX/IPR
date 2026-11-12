"""First bounded, paired synthetic-noise pilot; frozen truths, no production hook."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
import time

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation, difference, aggregate_differences
from bayer_canonical import predict, canonicalize
from clean_cfa_loss import clean_cfa_loss
from cfa_structure_loss import structure_loss
from continuation_policy import secondary_weight
from continuation_initialization import initialize_continuation
from compare_reconstruction_objectives import freeze_cases, confined
from confirm_local_route import MODEL_TOOLS, summarize
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction, FORWARD_TOOLS
from noisy_training_policy import configure_trainable
from prepare_closed_inference import prepare_inference
from reconstruction_loss import energy_balanced_loss
from run_balanced_moment_pilot import balanced_schedule
from run_diverse_moment_pilot import edge_image
from run_memory_bounded_reconstruction import SampleStream
from run_synthetic_joint_pilot import tensor_hash, metrics, aggregate
from synthetic_bayer_data import PATTERNS, FAMILIES, signal, noisy_observation
from training_resume import save_checkpoint, restore_checkpoint
from fine_texture_training import POLICY as TEXTURE_POLICY, texture
from training_texture_data import training_texture,training_context
from context_data import extend_signal, extend_edge, extend_case, extend_observation, crop_centre
from multiscale_correction import composed_support


NOISE = ((0., 0.), (.0002, .000025), (.0008, .0004))
VARIANTS = ('full', 'without_correction', 'parent')
MEMORY_POLICY = dict(hard_bytes=2*1024**3, soft_bytes=1280*1024**2,
    system_fraction=.92, interval=.25)
TOOLS = tuple(dict.fromkeys(MODEL_TOOLS+FORWARD_TOOLS+('run_noisy_moment_pilot.py', 'noisy_moment_unet.py',
    'memory_budget.py', 'prepare_closed_inference.py', 'reconstruction_loss.py',
    'compare_reconstruction_objectives.py', 'confirm_local_route.py',
    'run_synthetic_joint_pilot.py', 'reconstruction_gates.py', 'audit_bayer_symmetries.py',
    'training_resume.py', 'noisy_training_policy.py', 'noise_conditioning.py', 'fine_texture_training.py',
    'clean_cfa_loss.py', 'cfa_structure_loss.py', 'continuation_policy.py', 'continuation_initialization.py', 'context_data.py', 'texture_sampling.py', 'audit_texture_coverage.py', 'training_texture_data.py')))


def observation(truth, tags, noise_index, seed):
    a, b = NOISE[noise_index]
    return noisy_observation(truth, tags, a, b, torch.Generator().manual_seed(seed))


def runtime_versions():
    return dict(torch=str(torch.__version__), python=sys.version, dtype='float32', device='cpu')


def prepare(root, source, priors, parents, output, training_scope='cfa_front', preflight=None):
    if training_scope not in ('joint', 'cfa_front'):
        raise ValueError('Declared training scope required')
    confined(root, [source, output]+priors+parents)
    if preflight is None:
        raise ValueError('Successful expanded-context engineering preflight required before long training')
    confined(root, [preflight])
    engineering = json.loads(preflight.read_text(encoding='utf-8'))
    required_math = FORWARD_TOOLS+('context_data.py', 'clean_cfa_loss.py', 'fine_texture_training.py', 'prepare_closed_inference.py')
    if (engineering.get('status') != 'passed_multiscale_engineering_preflight_not_quality_qualification'
            or engineering.get('completed_steps') != 8 or engineering.get('support') != composed_support()
            or engineering.get('runtime') != runtime_versions()
            or any(engineering['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in required_math)):
        raise ValueError('Preflight did not qualify this context arithmetic/runtime')
    proof = json.loads((source/'result.json').read_text(encoding='utf-8'))
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Frozen training source required')
    parent_data = [json.loads((p/'result.json').read_text(encoding='utf-8')) for p in parents]
    if len(parent_data) != 2 or {p['freeze']['config']['seed'] for p in parent_data} != {71, 73}:
        raise ValueError('Both qualified initialization witnesses required')
    references = []
    for p, data in zip(parents, parent_data):
        config = data['freeze']['config']
        if (data['status'] != 'completed_local_route_pilot_not_noise_training'
                or config['objective'] != 'tempered' or config['steps'] != 2048
                or config['model_kind'] != 'local' or config['width'] != 8
                or config['scale'] != 2. or config['side'] != 128
                or not data['qualification20']['passed'] or not data['qualification80']['passed']
                or data['freeze']['source_result_sha256'] != digest(source/'result.json')
                or data['freeze']['gates'] != proof['freeze']['gates']
                or digest(p/'weights.pt') != data['weights_sha256']):
            raise ValueError('Qualified tempered final checkpoint required')
        if any(data['freeze']['tools'].get(n) != digest(Path(__file__).with_name(n)) for n in MODEL_TOOLS):
            raise ValueError('Reconstruction/data implementation changed')
        references.append(dict(seed=config['seed'], folder=str(p.resolve()),
            result_sha256=digest(p/'result.json'), weights_sha256=data['weights_sha256']))
    if engineering['parent_weights_sha256'] not in {p['weights_sha256'] for p in references}:
        raise ValueError('Preflight reconstruction parent differs')
    guard = MemoryGuard(**MEMORY_POLICY)
    with guard:
        torch.set_num_threads(4)
        old = [json.loads(p.read_text(encoding='utf-8')) for p in priors]
        seen = {key: [r for p in old for r in p[key]] for key in ('edges', 'mixed')}
        edges, mixed = freeze_cases(proof, seen, 256, 3500000)
        observed = []
        for index, case in enumerate(mixed+edges):
            guard.check()
            truth = edge_image(case) if 'family' not in case else signal(case['family'], case['seed'], 128)[None]
            for phase, values in enumerate(PATTERNS):
                tags = torch.tensor([values])
                for level in range(3):
                    noise_seed = 94000000+index*12+phase*3+level
                    packed, sigma = observation(truth, tags, level, noise_seed)
                    observed.append(dict(case_index=index, phase=phase, noise_index=level, noise_seed=noise_seed,
                        noisy_sha256=tensor_hash(packed), sigma_sha256=tensor_hash(sigma)))
        guard.check()
        schedule, _ = balanced_schedule(proof['freeze']['train_slots'], 2048, 4, 128)
        texture_events = []
        for step, entry in enumerate(schedule, start=1):
            for position, (index, phase) in enumerate(zip(entry['ids'], entry['phases'])):
                family = proof['freeze']['train_slots'][index]['family']
                if family in TEXTURE_POLICY['families']:
                    guard.check()
                    serial = 4*(step-1)+position
                    target, metadata = texture(family, serial, 128)
                    texture_events.append(dict(serial=serial, family=family, phase=phase,
                        target_sha256=tensor_hash(target), **metadata))
    plan = dict(schema_version=4, frozen_at_utc=datetime.now(timezone.utc).isoformat(),
        source=str(source.resolve()), source_result_sha256=digest(source/'result.json'), parents=references,
        prior_plan_sha256=[digest(p) for p in priors], edges=edges, mixed=mixed, observations=observed,
        noise_levels=[dict(a=a, b=b) for a, b in NOISE],
        config=dict(steps=2048, microbatch=1, accumulation=4, learning_rate=.001, scale=2., width=8,
            noise_hidden_width=8, side=256, source_side=128, loss_border=112, training_scope=training_scope),
        context_policy=dict(support=composed_support(), validation_side=320,
            training_halo_seed_base=170000000, validation_halo_seed_base=190000000,
            policy='Analytic continuation, immutable centre128 RGB/CFA/sigma, independent halo noise. Score same32pixel centre as before; no reflection/resizing.'),
        engineering_preflight=dict(path=str(preflight.resolve()), sha256=digest(preflight), campaign_id=engineering['campaign_id']),
        gates=proof['freeze']['gates'], memory_policy=MEMORY_POLICY,
        runtime=runtime_versions(),
        conditioning='Two-level equivariant noise U-Net width8/16/32, forty input features including quadratic standardized residual; zero head, bound3sigma, frozen reconstruction. Expanded context, same scored centre.',
        texture_augmentation=dict(policy=TEXTURE_POLICY, events=texture_events),
        clean_fixed_point=dict(weight=1., scope='Every positive-noise training exposure; clean CFA with the same observed-sigma field.',
            target='Zero CFA correction on clean signal, no RGB interpolation compensation.',
            normalization='Same noisy RGB reference energy and tempered floor as the data term.'),
        structure_preservation=dict(weight=1., scope='Positive-noise CFA core, four physical planes.',
            policy='Average squared CFA error projections onto centred clean target and noise-orthogonal target curvature; independent-noise variance normalization, relative rank guards1e-8/1e-4; constants zero.'),
        tools={n: digest(Path(__file__).with_name(n)) for n in TOOLS},
        noise_gate_policy='Each positive level: RGB MSE full/parent<=.98, opponent full/parent<=1, full/without_correction RGB<=1; weak/edge gradient>=.95 parent. Zero noise retains original12gates, new mixed and historical20/80.',
        training_policy='Final2048 only, both pretrained seeds, same ID/phase schedule; replace wave slots by frozen broadband two-wave textures, other slots unchanged. Noise level=(4*(step-1)+position)%3, seed74000000+4*(step-1)+position, paired realizations across models. Accumulated global gradient L2 norm clipped to1 before Adam; no early stopping. Adam scope='+training_scope,
        checkpoint_policy=dict(interval_steps=64, boundary='After completed Adam step only; restart partial microbatch from last committed checkpoint.',
            selection='One atomic latest checkpoint, no validation scores or best-checkpoint selection.',
            state='CPU model, Adam, completed step, losses, exposure counts, torch CPU RNG; data/noise draws derive deterministically from step.'),
        limits=['Independent Gaussian CFA noise with variance a*max(clean signal,0)+b, before demosaicing; not a sensor calibration.',
            'Sigma estimated from observed signal and known simulation coefficients, not from clean truth.',
            'Noisy bilinear reference in tempered loss; its energy includes noise as well as content.',
            'Three-sigma bounded front correction; joint fine tuning can alter the trunk, cfa_front freezes it and skips gradient-free zero-noise anchors.',
            'Frozen parent has fewer training steps; disabling correction in the trained model isolates the front-end contribution but is not an independently optimized witness.',
            'Same procedural families and fixed4phase generator, not photographic validation.'], preparation_memory=guard.stats())
    output.mkdir(parents=True, exist_ok=False)
    save(output/'plan.json', plan)
    print('PREPARED '+str(output/'plan.json'), flush=True)


def train(model, source, config, guard, checkpoint_path, context, progress, interval_steps, resume=None, texture_events=()):
    if isinstance(interval_steps, bool) or not isinstance(interval_steps, int) or not 1 <= interval_steps <= config['steps']:
        raise ValueError('Positive checkpoint interval within training horizon required')
    schedule, _ = balanced_schedule(source['freeze']['train_slots'], config['steps'], 4, config.get('source_side', config['side']))
    if tensor_hash(torch.tensor([[*e['ids'], *e['phases']] for e in schedule])) != source['freeze']['phase_schedule_sha256']:
        raise ValueError('Training schedule changed')
    stream = SampleStream(source)
    events = {e['serial']: e for e in texture_events}
    if len(events) != len(texture_events):
        raise ValueError('Duplicate training texture event')
    expected = {4*(step-1)+position for step, entry in enumerate(schedule, start=1)
        for position, index in enumerate(entry['ids'])
        if source['freeze']['train_slots'][index]['family'] in TEXTURE_POLICY['families']}
    if set(events) != expected:
        raise ValueError('Exact frozen training texture coverage required')
    optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
    completed, losses, counts = 0, [], {}
    if resume is not None:
        completed, losses, counts, meta = restore_checkpoint(resume, model, optimizer, context)
        progress.update(completed_optimizer_steps=completed, restored_from=meta, checkpoint=meta)
    if completed == 0:
        progress['checkpoint'] = save_checkpoint(checkpoint_path, model, optimizer, context, 0, losses, counts)
    for step, entry in enumerate(schedule, start=1):
        if step <= completed:
            continue
        guard.check()
        model.train()
        optimizer.zero_grad(set_to_none=True)
        total = 0.
        for position, (index, phase) in enumerate(zip(entry['ids'], entry['phases'])):
            guard.check()
            serial = 4*(step-1)+position
            family = source['freeze']['train_slots'][index]['family']
            if family in TEXTURE_POLICY['families']:
                event = events.get(serial)
                if event is None or event['family'] != family or event['phase'] != phase:
                    raise ValueError('Missing or mismatched frozen training texture')
                truth, _ = training_texture(event)
                if tensor_hash(truth) != event['target_sha256']:
                    raise ValueError('Frozen training texture changed')
                tags = torch.tensor([PATTERNS[phase]])
            else:
                truth, _, tags = stream.sample(step, position, index, phase)
            level = serial % 3
            packed, sigma = observation(truth, tags, level, 74000000+serial)
            if config['side'] > truth.shape[-1]:
                event = stream.events.get((step, position))
                if family in TEXTURE_POLICY['families']:
                    expanded = training_context(events[serial], config['side'])
                elif event is not None:
                    expanded = extend_edge(event, config['side'])
                else:
                    expanded = extend_signal(family, source['freeze']['train_slots'][index]['seed'], config['side'])
                if not torch.equal(crop_centre(expanded), truth):
                    raise ValueError('Training context centre changed')
                packed, sigma = extend_observation(expanded, tags, packed, sigma, *NOISE[level], 170000000+serial)
                truth = expanded
            image = predict(model, packed/config['scale'], sigma/config['scale'], tags, False, True)
            reference = bilinear(packed/config['scale'], tags)
            loss = energy_balanced_loss(image, truth/config['scale'], reference, config['scale'],
                border=config.get('loss_border', 48), objective='tempered')/4
            if level != 0:
                clean_values, deviations, labels, _ = canonicalize(sample_rgb(truth, tags)/config['scale'],
                    sigma/config['scale'], tags)
                anchored = model.correct_observations(clean_values, deviations, labels)
                loss = loss+clean_cfa_loss(anchored, clean_values, reference,
                    truth/config['scale'], config['scale'], border=config.get('loss_border', 48))/4
                noisy_values, _, _, _ = canonicalize(packed/config['scale'], sigma/config['scale'], tags)
                corrected = model.correct_observations(noisy_values, deviations, labels)
                weight = secondary_weight(config, step)
                loss = loss+structure_loss(corrected, clean_values, deviations,
                    config.get('loss_border', 48)//2, weight)/4
                del noisy_values, corrected
                del clean_values, deviations, labels, anchored
            if not torch.isfinite(loss):
                raise ValueError('Nonfinite loss')
            if loss.requires_grad:
                loss.backward()
            elif level != 0:
                raise ValueError('No trainable gradient for noisy observation')
            total += float(loss.detach())
            key = f'{family}/phase{phase}/noise{level}'
            counts[key] = counts.get(key, 0)+1
            del image, loss, truth, packed, sigma, tags, reference
        if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
            raise ValueError('Nonfinite gradient')
        norm = torch.nn.utils.clip_grad_norm_(model.parameters(), 1., error_if_nonfinite=True)
        progress['last_preclip_gradient_norm'] = float(norm)
        optimizer.step()
        losses.append(total)
        progress['completed_optimizer_steps'] = step
        if step % interval_steps == 0 or step == config['steps']:
            optimizer.zero_grad(set_to_none=True)
            progress['checkpoint'] = save_checkpoint(checkpoint_path, model, optimizer, context, step, losses, counts)
        if step % 512 == 0:
            print('STEP '+str(step)+'/'+str(config['steps'])+' memory='+json.dumps(guard.stats()), flush=True)
    optimizer.zero_grad(set_to_none=True)
    model.eval().requires_grad_(False)
    return losses, counts


def variant_summary(rows, variant):
    return aggregate([dict(metrics=r['metrics'][variant]) for r in rows])


def zero_gates(rows, gates):
    return summarize([dict(r, metrics=r['metrics']['full']) for r in rows], gates)


def noise_verdict(rows):
    """Predeclared comparisons, including the contribution of the correction."""
    verdicts = []
    for level in (1, 2):
        group = [r for r in rows if r['noise_index'] == level]
        summaries = {v: variant_summary(group, v) for v in VARIANTS}
        rgb = summaries['full']['output_mse']/summaries['parent']['output_mse']
        ablated = summaries['full']['output_mse']/summaries['without_correction']['output_mse']
        opp = sum(r['metrics']['full']['output_opponent_sse'] for r in group)/sum(r['metrics']['parent']['output_opponent_sse'] for r in group)
        checks = dict(rgb_gain_vs_parent=rgb <= .98, opponent_not_worse_vs_parent=opp <= 1.,
            correction_contributes=ablated <= 1.)
        for family in ('edge', 'weak_achromatic_wave', 'chromatic_wave'):
            selected = [r for r in group if r['family'] == family]
            current, parent = (variant_summary(selected, v) for v in ('full', 'parent'))
            checks[family+'_gradient'] = current['output_gradient_gain'] >= .95*parent['output_gradient_gain']
        verdicts.append(dict(noise_index=level, summaries=summaries, rgb_full_over_parent=rgb,
            opponent_full_over_parent=opp, rgb_full_over_without_correction=ablated, checks=checks, passed=all(checks.values())))
    return verdicts


def evaluate(model, parent, plan, source, guard):
    prepared = prepare_inference(model)
    rows, historical, symmetries = [], [], []
    scale = plan['config']['scale']
    lookup = {(r['case_index'], r['phase'], r['noise_index']): r for r in plan['observations']}
    with torch.inference_mode():
        for index, case in enumerate(plan['mixed']+plan['edges']):
            family = case.get('family', 'edge')
            truth = signal(family, case['seed'], 128)[None] if 'family' in case else edge_image(case)
            if tensor_hash(truth) != case['target_sha256']:
                raise ValueError('Frozen truth changed')
            expanded_truth = extend_case(case, plan['context_policy']['validation_side'])
            if not torch.equal(crop_centre(expanded_truth), truth):
                raise ValueError('Validation context changed historical truth')
            for phase, values in enumerate(PATTERNS):
                tags = torch.tensor([values])
                clean = sample_rgb(truth, tags)
                if tensor_hash(clean) != case['packed_sha256_by_phase'][phase]:
                    raise ValueError('Frozen clean observation changed')
                for level in range(3):
                    guard.check()
                    frozen = lookup[index, phase, level]
                    packed, sigma = observation(truth, tags, level, frozen['noise_seed'])
                    source_hash = tensor_hash(packed)
                    if source_hash != frozen['noisy_sha256'] or tensor_hash(sigma) != frozen['sigma_sha256']:
                        raise ValueError('Frozen noisy observation changed')
                    halo_seed = plan['context_policy']['validation_halo_seed_base']+frozen['noise_seed']
                    context_values, context_sigma = extend_observation(expanded_truth, tags, packed, sigma,
                        *NOISE[level], halo_seed)
                    args = (context_values/scale, context_sigma/scale, tags, False, True)
                    image = crop_centre(predict(prepared, *args)*scale)
                    original = crop_centre(predict(model, *args)*scale)
                    if not torch.equal(image, original):
                        raise ValueError('Prepared output differs')
                    disabled = crop_centre(predict(model, context_values/scale, torch.zeros_like(context_sigma), tags, False, True)*scale)
                    witness = crop_centre(predict(parent, context_values/scale, torch.zeros_like(context_sigma), tags, True, True)*scale)
                    old_witness = predict(parent, packed/scale, torch.zeros_like(sigma), tags, True, True)*scale
                    bridge_error = float((witness-old_witness)[:, :, 48:-48, 48:-48].abs().max())
                    if bridge_error > 2e-6:
                        raise ValueError('Expanded parent changed the scored historical core')
                    base = bilinear(packed, tags)
                    measured = {v: metrics(truth, base, out) for v, out in zip(VARIANTS, (image, disabled, witness))}
                    sites = sample_rgb(image, tags)
                    row = dict(family=family, seed=case['seed'], case_index=index,
                        mixed='family' in case, phase_index=phase, noise_index=level, noise_seed=frozen['noise_seed'],
                        designated_phase=phase == case.get('original_phase_index'),
                        target_sha256=case['target_sha256'], noisy_sha256=source_hash, sigma_sha256=frozen['sigma_sha256'],
                        context_side=expanded_truth.shape[-1], context_halo_seed=halo_seed,
                        context_target_sha256=tensor_hash(expanded_truth), context_noisy_sha256=tensor_hash(context_values),
                        context_sigma_sha256=tensor_hash(context_sigma), parent_core_bridge_maximum=bridge_error,
                        output_sha256={v: tensor_hash(out) for v, out in zip(VARIANTS, (image, disabled, witness))},
                        observed_site_mse=float((sites-packed).square().mean()),
                        restored_site_mse=float((sites-clean).square().mean()),
                        noisy_site_mse=float((packed-clean).square().mean()),
                        negative_output_fraction=float((image < 0).float().mean()),
                        above_one_output_fraction=float((image > 1).float().mean()), metrics=measured)
                    if level == 0 and (not torch.equal(image, disabled) or not torch.equal(sites, packed)):
                        raise ValueError('Zero-noise observation contract broken')
                    if level == 0 and plan['config']['training_scope'] == 'cfa_front' and not torch.equal(image, witness):
                        raise ValueError('Frozen reconstruction no longer matches parent')
                    if tensor_hash(packed) != source_hash:
                        raise ValueError('Input changed')
                    rows.append(row)
                    if index == len(plan['mixed']) and level == 2:
                        for op in OPERATIONS:
                            guard.check()
                            tp, labels = transform_observation(context_values, tags, op)
                            ts, _ = transform_observation(context_sigma, tags, op)
                            restored = crop_centre(transform(predict(prepared, tp/scale, ts/scale, labels, False, True)*scale, op, True))
                            score = difference(image, restored)
                            if score['max_absolute_difference'] > 2e-6:
                                raise ValueError('Learned noisy symmetry tolerance exceeded')
                            symmetries.append(dict(phase_index=phase, operation=list(op), **score))
        for case in source['freeze']['evaluation_cases']:
            truth = signal(case['family'], case['seed'], 128)[None]
            for phase, values in enumerate(PATTERNS):
                guard.check()
                tags = torch.tensor([values])
                packed = sample_rgb(truth, tags)
                output = predict(prepared, packed/scale, torch.zeros_like(packed), tags, False, True)*scale
                if plan['config']['training_scope'] == 'cfa_front':
                    witness = predict(parent, packed/scale, torch.zeros_like(packed), tags, True, True)*scale
                    if not torch.equal(output, witness):
                        raise ValueError('Frozen historical reconstruction differs from parent')
                historical.append(dict(family=case['family'], seed=case['seed'], phase_index=phase,
                    original_phase=list(values) == case['pattern'], target_sha256=tensor_hash(truth),
                    packed_sha256=tensor_hash(packed), output_sha256=tensor_hash(output),
                    observed_site_mse=float((sample_rgb(output, tags)-packed).square().mean()),
                    metrics=dict(full=metrics(truth, bilinear(packed, tags), output))))
    mixed_zero = [r for r in rows if r['mixed'] and r['noise_index'] == 0]
    result = dict(records=rows, historical_records=historical, symmetry=aggregate_differences(symmetries),
        prepared_bit_exact_cases=len(rows), zero_new=zero_gates(mixed_zero, plan['gates']),
        zero_new20=zero_gates([r for r in mixed_zero if r['designated_phase']], plan['gates']),
        zero_historical80=zero_gates(historical, plan['gates']),
        zero_historical20=zero_gates([r for r in historical if r['original_phase']], plan['gates']),
        noise_mixed=noise_verdict([r for r in rows if r['mixed']]),
        by_noise_family={str(level): {f: {v: variant_summary([r for r in rows if r['mixed'] and r['noise_index'] == level and r['family'] == f], v)
            for v in VARIANTS} for f in FAMILIES} for level in range(3)},
        distinct_edges={str(level): {v: variant_summary([r for r in rows if not r['mixed'] and r['noise_index'] == level], v)
            for v in VARIANTS} for level in range(3)},
        noise_all=noise_verdict(rows))
    edge_zero = result['distinct_edges']['0']['full']
    result['zero_distinct_edge_checks'] = dict(rgb_not_worse=edge_zero['mse_ratio'] <= 1.,
        opponent_not_worse=edge_zero['opponent_mse_ratio'] <= 1.,
        gradient_preserved=edge_zero['output_gradient_gain'] >= .95*edge_zero['baseline_gradient_gain'])
    result['pilot_gates_passed'] = (all(result[k]['qualification']['passed'] for k in
        ('zero_new', 'zero_new20', 'zero_historical80', 'zero_historical20'))
        and all(result['zero_distinct_edge_checks'].values())
        and all(v['passed'] for v in result['noise_mixed']+result['noise_all']))
    return result


def run(root, plan_path, seed, output, resume=None):
    confined(root, [plan_path, output]+([resume] if resume is not None else []))
    plan = json.loads(plan_path.read_text(encoding='utf-8'))
    if plan.get('schema_version') != 4 or plan.get('context_policy', {}).get('support') != composed_support():
        raise ValueError('Qualified multi-scale context plan required')
    if plan['memory_policy'] != MEMORY_POLICY:
        raise ValueError('Declared memory policy differs from current authorized policy')
    if plan['runtime'] != runtime_versions():
        raise ValueError('Python or Torch runtime differs from qualification plan')
    if plan['texture_augmentation']['policy'] != TEXTURE_POLICY:
        raise ValueError('Declared training texture policy changed')
    if plan['clean_fixed_point']['weight'] != 1.:
        raise ValueError('Current fixed-point regularizer must be declared')
    tools = plan['tools']
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Frozen implementation changed')
    reference = next((r for r in plan['parents'] if r['seed'] == seed), None)
    if reference is None:
        raise ValueError('Declared seed required')
    folder, source_path = Path(reference['folder']), Path(plan['source'])
    confined(root, [folder, source_path])
    if (digest(source_path/'result.json') != plan['source_result_sha256']
            or digest(folder/'result.json') != reference['result_sha256']
            or digest(folder/'weights.pt') != reference['weights_sha256']):
        raise ValueError('Frozen source or parent changed')
    source = json.loads((source_path/'result.json').read_text(encoding='utf-8'))
    now = datetime.now(timezone.utc)
    campaign = 'neural-noisy-moment-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        plan=plan, plan_sha256=digest(plan_path), seed=seed, status='incomplete',
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip())
    progress = dict(completed_optimizer_steps=0, checkpoint=None)
    context = dict(plan_sha256=result['plan_sha256'], seed=seed, config=plan['config'],
        parent_weights_sha256=reference['weights_sha256'], source_result_sha256=plan['source_result_sha256'])
    guard, started = MemoryGuard(**plan['memory_policy']), time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            result['runtime'] = dict(runtime_versions(), intra_threads=torch.get_num_threads(),
                interop_threads=torch.get_num_interop_threads(), deterministic=torch.are_deterministic_algorithms_enabled())
            torch.manual_seed(seed)
            checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
            proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
            if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
                raise ValueError('Parent checkpoint provenance differs')
            model = NoisyMomentUNet(plan['config']['width'])
            if model.noise_head.in_channels != plan['config']['noise_hidden_width']:
                raise ValueError('Declared correction hidden width differs')
            load_reconstruction(model, checkpoint['state_dict'])
            if 'warm_start' in plan:
                warm = plan['warm_start']
                refs = [r for r in warm['models'] if r['seed'] == seed]
                if len(refs) != 1 or warm['completed_optimizer_steps'] != 2048 or plan['config']['steps'] != 512:
                    raise ValueError('Declared2048+512 continuation required')
                ref = refs[0]
                initialize_continuation(root, model, checkpoint['state_dict'], ref)
                result['warm_start'] = ref
            result['training_scope'] = configure_trainable(model, plan['config']['training_scope'],
                plan['config'].get('statistics_trainable', True))
            result['parameter_count'] = sum(p.numel() for p in model.parameters())
            result['losses'], result['training_exposure_counts'] = train(model, source, plan['config'], guard,
                output/'training-latest.pt', context, progress, plan['checkpoint_policy']['interval_steps'], resume,
                plan['texture_augmentation']['events'])
            progress['total_optimizer_steps'] = progress['completed_optimizer_steps']+plan.get('warm_start', {}).get('completed_optimizer_steps', 0)
            parent = LocalMomentUNet(plan['config']['width']).eval().requires_grad_(False)
            parent.load_state_dict(checkpoint['state_dict'], strict=True)
            result.update(evaluate(model, parent, plan, source, guard))
            if any(not torch.equal(v, model.state_dict()[n]) for n, v in checkpoint['state_dict'].items()):
                raise ValueError('Frozen parent changed')
            torch.save(dict(state_dict=model.state_dict(), config=plan['config'], seed=seed,
                plan_sha256=result['plan_sha256']), output/'weights.pt')
            result['weights_sha256'] = digest(output/'weights.pt')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
                raise ValueError('Implementation changed during pilot')
            result['status'] = 'completed_synthetic_noise_pilot_not_photographic_qualification'
    except (BudgetExceeded, MemoryError) as error:
        result['status'] = 'aborted_for_memory_budget'
        result['error'] = str(error)
        raise
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['training_progress'] = progress
        result['memory'] = guard.stats()
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print('COMPLETE '+campaign+' gates='+str(result['pilot_gates_passed']), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--source', type=Path)
    parser.add_argument('--prior', nargs='+', type=Path)
    parser.add_argument('--parents', nargs=2, type=Path)
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--seed', type=int, choices=(71, 73))
    parser.add_argument('--resume', type=Path)
    parser.add_argument('--training-scope', choices=('joint', 'cfa_front'), default='cfa_front')
    parser.add_argument('--preflight', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.prepare:
        if args.source is None or not args.prior or not args.parents:
            parser.error('Preparation requires source, prior plans and both parents')
        prepare(root, args.source, args.prior, args.parents, args.output, args.training_scope, args.preflight)
    elif args.plan is None or args.seed is None:
        parser.error('Training requires frozen plan and declared seed')
    else:
        run(root, args.plan, args.seed, args.output, args.resume)
