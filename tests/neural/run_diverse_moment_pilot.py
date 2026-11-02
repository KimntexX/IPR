"""Fixed-budget contour-diversity ablation; final checkpoint, no noise training."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import subprocess
import time

import torch

from audit_bayer_phases import PHASE_NAMES
from bayer_canonical import predict
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb
from moment_bayer_unet import model_from_config
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import balanced_schedule, angular_signal, measure, summaries
from run_synthetic_joint_pilot import tensor_hash, aggregate
from synthetic_bayer_data import PATTERNS, signal, noisy_observation


def edge_recipe(index, split):
    """Stratified normals, independently jittered positions and RGB contrasts."""
    if split not in ('train', 'reserved') or not isinstance(index, int) or index < 0:
        raise ValueError('Nonnegative index and explicit data partition required')
    seed = (1000000 if split == 'train' else 2000000)+index
    u = torch.rand(10, generator=torch.Generator().manual_seed(seed)).tolist()
    normal_bin = (index*13) % 32
    angle = (normal_bin+.15+.7*u[0])*180/32
    low, high = [.03+.94*v for v in u[2:5]], [.03+.94*v for v in u[5:8]]
    if u[8] < .25:
        low, high = [low[0]]*3, [high[0]]*3
    return dict(split=split, seed=seed, normal_bin=normal_bin, normal_degrees=angle,
        offset=-.15+.3*u[1], low_rgb=low, high_rgb=high)


def edge_image(recipe, side=128):
    low, high = torch.tensor(recipe['low_rgb']), torch.tensor(recipe['high_rgb'])
    if (low.shape != (3,) or high.shape != (3,) or not torch.isfinite(low).all() or not torch.isfinite(high).all()
            or not 0 <= recipe['normal_degrees'] < 180 or not math.isfinite(recipe['offset'])
            or side < 32 or side % 2):
        raise ValueError('Finite RGB colours, normal and offset with even side>=32 required')
    y, x = torch.meshgrid(torch.linspace(-1, 1, side), torch.linspace(-1, 1, side), indexing='ij')
    angle = recipe['normal_degrees']*math.pi/180
    mask = (x*math.cos(angle)+y*math.sin(angle) > recipe['offset'])[None, None]
    return torch.where(mask, high[None, :, None, None], low[None, :, None, None]).float()


def freeze_edge(recipe, side):
    truth = edge_image(recipe, side)
    return dict(**recipe, target_sha256=tensor_hash(truth),
        packed_sha256_by_phase=[tensor_hash(sample_rgb(truth, torch.tensor([p]))) for p in PATTERNS])


def validate_edge_exposures(events):
    counts = {}
    for event in events:
        phase = event['phase_index']
        if phase not in range(4):
            raise ValueError('Invalid training phase')
        values = counts.setdefault(event['recipe_index'], [0]*4)
        values[phase] += 1
        if max(values) > 1:
            raise ValueError('Edge recipe repeated before covering distinct phases')
    if not counts:
        raise ValueError('Nonempty edge exposures required')


def run(parent, output):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    config = dict(proof['freeze']['config'])
    weights = parent/'synthetic-joint-weights.pt'
    if (proof['status'] != 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
            or config['model_kind'] != 'moment' or not config['canonical_input'] or not config['exact_observations']
            or digest(weights) != proof['weights_sha256'] or 'phase_counts_by_content' not in proof['freeze']):
        raise ValueError('Expected completed balanced moment reference')
    for name, expected in proof['freeze']['tools'].items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed parent dependency: '+name)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    cases = proof['freeze']['train_cases']
    tests = proof['freeze']['evaluation_cases']
    if len(cases) != 40 or len(tests) != 20 or sum(c['family'] == 'edge' for c in cases) != 8:
        raise ValueError('Expected 40 slots, including8 edge slots, and20 historical controls')
    schedule, counts = balanced_schedule(cases, config['steps'], config['batch'], config['side'])
    schedule_hash = tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule]))
    if schedule_hash != proof['freeze']['phase_schedule_sha256'] or counts.tolist() != proof['freeze']['phase_counts_by_content']:
        raise ValueError('Schedule or exposure drift')
    events, probe, seen_bins = [], [], set()
    slot_visits, slot_edges, recipe_count = {}, {}, 0
    for step, item in enumerate(schedule):
        item['edge_events'] = {}
        for position, index in enumerate(item['ids']):
            if cases[index]['family'] == 'edge':
                visits = slot_visits.get(index, 0)
                if visits % 4 == 0:
                    slot_edges[index] = freeze_edge(edge_recipe(recipe_count, 'train'), config['side'])
                    slot_edges[index]['recipe_index'] = recipe_count
                    recipe_count += 1
                slot_visits[index] = visits+1
                event = dict(slot_edges[index])
                event.update(step=step+1, batch_position=position, slot=index, phase_index=item['phases'][position])
                item['edge_events'][position] = len(events)
                events.append(event)
                if event['normal_bin'] not in seen_bins:
                    seen_bins.add(event['normal_bin'])
                    probe.append(len(events)-1)
    validate_edge_exposures(events)
    if len(seen_bins) != 32:
        raise ValueError('Training normal strata incomplete')
    reserved = [freeze_edge(edge_recipe(i, 'reserved'), config['side']) for i in range(32)]
    train_hashes = {e['target_sha256'] for e in events}
    if any(e['target_sha256'] in train_hashes for e in reserved):
        raise ValueError('Training/reserved target overlap')
    now = datetime.now(timezone.utc)
    campaign = 'neural-diverse-moment-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(config=config, train_slots=cases, evaluation_cases=tests, train_edge_events=events,
        train_probe_event_indices=probe, reserved_edges=reserved,
        phase_schedule_sha256=schedule_hash, phase_counts_by_slot=counts.tolist(),
        reference_result_sha256=digest(parent/'result.json'), reference_weights_sha256=digest(weights),
        gates=proof['freeze']['gates'], git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        tools={**proof['freeze']['tools'], Path(__file__).name: digest(Path(__file__))},
        distinct_training_edges=recipe_count,
        policy='32 unchanged non-edge contents;8 edge slots refreshed every4 exposures; same ID/phase schedule and optimization budget.',
        selection='Final checkpoint only;32 reserved edge truths frozen before training, never used in optimization.',
        reserved_checks='Existing edge RGB/opponent<=1 and gradient>=.95*bilinear; no new fitted threshold.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    torch.manual_seed(config['seed'])
    model = model_from_config(config)
    optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
    reference = model_from_config(config)
    checkpoint = torch.load(weights, map_location='cpu', weights_only=True)
    if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
        raise ValueError('Checkpoint provenance differs')
    reference.load_state_dict(checkpoint['state_dict'], strict=True)
    reference.requires_grad_(False)
    reference.eval()
    clean = torch.stack([signal(c['family'], c['seed'], config['side']) for c in cases])
    generator = torch.Generator().manual_seed(72)
    rows, groups, losses = [], {n: [] for n in ('crossed', 'angular', 'reserved', 'train_probe')}, []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, freeze_sha256=digest(output/'freeze.json'), status='incomplete', records=rows,
        groups=groups, losses=losses, threads_intra=4, parameter_count=sum(p.numel() for p in model.parameters()),
        versions=dict(torch=torch.__version__), limits=[
            'Synthetic camera RGB, no sensor model or denoising/photographic qualification.',
            'Training diversity is a bundled data change: angle, position, contrast and number of distinct edge truths.',
            '32 fixed non-edge contents plus dynamic edge slots, not the prior40 fixed training images.',
            'Reserved controls share a generator and are consulted once here; not independent natural-scene evidence.',
            'Training probe is32 preselected actually exposed edges, not the complete training objective.'])
    started = time.monotonic()
    try:
        for step, item in enumerate(schedule, start=1):
            ids = torch.randint(len(cases), (config['batch'],), generator=generator)
            if ids.tolist() != item['ids']:
                raise ValueError('Training ID RNG drift')
            target = clean[ids]
            for position, event_index in item['edge_events'].items():
                event = events[event_index]
                truth = edge_image(event, config['side'])
                if tensor_hash(truth) != event['target_sha256']:
                    raise ValueError('Training edge changed')
                target[position] = truth[0]
            pattern = torch.tensor([PATTERNS[p] for p in item['phases']], dtype=torch.int64)
            noisy, sigma = noisy_observation(target, pattern, 0., 0., generator)
            for position, event_index in item['edge_events'].items():
                if tensor_hash(noisy[position:position+1]) != events[event_index]['packed_sha256_by_phase'][item['phases'][position]]:
                    raise ValueError('Training observation changed')
            model.train()
            optimizer.zero_grad(set_to_none=True)
            image = predict(model, noisy/config['scale'], sigma, pattern, exact_observations=True, canonical_input=True)
            loss = (image[:, :, 48:-48, 48:-48]-target[:, :, 48:-48, 48:-48]/config['scale']).square().mean()
            loss.backward()
            if not torch.isfinite(loss) or not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
                raise ValueError('Nonfinite training loss/gradient')
            optimizer.step()
            losses.append(float(loss.detach()))
            if step % 512 == 0:
                print(f'TRAIN {step}/{config["steps"]}', flush=True)
        model.eval()

        def evaluate(group, target, metadata, phases=range(4), expected=None):
            for phase in phases:
                pattern = torch.tensor([PATTERNS[phase]], dtype=torch.int64)
                current = measure(model, target, pattern, config['scale'])
                previous = measure(reference, target, pattern, config['scale'])
                if expected is not None:
                    old = expected[phase]
                    if any(previous[k] != old[k] for k in ('target_sha256', 'packed_sha256', 'output_sha256')):
                        raise ValueError('Reference regression replay differs')
                if 'target_sha256' in metadata and (current['target_sha256'] != metadata['target_sha256']
                        or current['packed_sha256'] != metadata['packed_sha256_by_phase'][phase]):
                    raise ValueError('Frozen evaluation truth/observation changed')
                groups[group].append(dict(**{k: v for k, v in metadata.items()
                    if k not in ('target_sha256', 'packed_sha256_by_phase')},
                    pattern=list(PATTERNS[phase]), phase_name=PHASE_NAMES[phase], **current,
                    reference_metrics=previous['metrics']))

        with torch.inference_mode():
            for case in tests:
                target = signal(case['family'], case['seed'], config['side'])[None]
                expected = [next(r for r in proof['crossed_records'] if r['family'] == case['family']
                    and r['seed'] == case['seed'] and r['phase_name'] == PHASE_NAMES[p]) for p in range(4)]
                evaluate('crossed', target, dict(family=case['family'], seed=case['seed']), expected=expected)
                rows.extend(r for r in groups['crossed'][-4:] if r['pattern'] == case['pattern'])
            for case in proof['freeze']['angular_cases']:
                target = angular_signal(case['normal_degrees'], case['offset'], config['side'])
                expected = [next(r for r in proof['angular_records'] if r['normal_degrees'] == case['normal_degrees']
                    and r['offset'] == case['offset'] and r['phase_name'] == PHASE_NAMES[p]) for p in range(4)]
                evaluate('angular', target, case, expected=expected)
            for case in reserved:
                evaluate('reserved', edge_image(case, config['side']), dict(family='edge', **case))
            for index in probe:
                case = events[index]
                evaluate('train_probe', edge_image(case, config['side']), dict(family='edge', **case), phases=(case['phase_index'],))
        result.update(summaries(rows))
        result['qualification'] = qualifies(result, freeze['gates'])
        result['summaries'] = {name: dict(**summaries(records),
            reference=summaries([dict(family=r['family'], metrics=r['reference_metrics']) for r in records]),
            by_phase={p: aggregate([r for r in records if r['phase_name'] == p]) for p in PHASE_NAMES
                if any(r['phase_name'] == p for r in records)}) for name, records in groups.items()}
        result['crossed_qualification'] = qualifies(dict(records=groups['crossed'], **summaries(groups['crossed'])), freeze['gates'])
        reserved_score = result['summaries']['reserved']['summary']
        gates = freeze['gates']
        result['reserved_edge_checks'] = dict(rgb=reserved_score['mse_ratio'] <= gates['edge_mse_ratio_max'],
            opponent=reserved_score['opponent_mse_ratio'] <= gates['edge_opponent_ratio_max'],
            gradient=reserved_score['output_gradient_gain'] >= gates['gradient_gain_fraction_min']*reserved_score['baseline_gradient_gain'])
        torch.save(dict(state_dict=model.state_dict(), config=config, freeze_sha256=result['freeze_sha256']),
            output/'synthetic-joint-weights.pt')
        result['weights_sha256'] = digest(output/'synthetic-joint-weights.pt')
        if digest(weights) != freeze['reference_weights_sha256'] or any(
                digest(Path(__file__).with_name(n)) != h for n, h in freeze['tools'].items()):
            raise ValueError('Reference or implementation changed')
        result['status'] = 'completed_diverse_contour_reconstruction_not_photographic_qualification'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(dict(qualification=result['qualification'], crossed=result['summaries']['crossed']['summary'],
        reserved=result['summaries']['reserved']['summary'], reserved_checks=result['reserved_edge_checks']), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.output)
