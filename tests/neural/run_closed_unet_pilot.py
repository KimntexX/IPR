"""Frozen-data36-channel pilot; optional structural equivariance, fixed final step."""
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
from closed_moment_unet import build_model
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import balanced_schedule, angular_signal, measure, summaries
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal, noisy_observation


def run(parent, output, kind, seed):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Expected frozen diversity data')
    tools = {**proof['freeze']['tools'], **{n: digest(Path(__file__).with_name(n)) for n in
        ('closed_moment_operator.py', 'closed_moment_unet.py', 'audit_bayer_symmetries.py', 'run_closed_unet_pilot.py')}}
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Source dependency changed')
    config = dict(proof['freeze']['config'], model_kind=kind, seed=seed)
    cases = proof['freeze']['train_slots']
    schedule, counts = balanced_schedule(cases, config['steps'], config['batch'], config['side'])
    schedule_hash = tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule]))
    if schedule_hash != proof['freeze']['phase_schedule_sha256']:
        raise ValueError('Training schedule changed')
    events = {(e['step'], e['batch_position']): e for e in proof['freeze']['train_edge_events']}
    now = datetime.now(timezone.utc)
    campaign = 'neural-'+kind+'-unet-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(config=config, source_result_sha256=digest(parent/'result.json'), tools=tools,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        gates=proof['freeze']['gates'], phase_schedule_sha256=schedule_hash, phase_counts=counts.tolist(),
        policy='Same source32fixed/418edge training truths and phases; same2048steps/Adam; final checkpoint only.',
        structural_policy='Projected convolution weights/biases follow explicit transpose channel representations.' if kind == 'equivariant'
            else 'Ordinary convolution trunk, completed36-coefficient interpolation basis.',
        confirmation_policy='Reserved recipe indices64..95 (seeds2000064..2000095) excluded here; confirm only final structure, never choose best seed.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    torch.manual_seed(seed)
    model = build_model(kind, config['width'])
    optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
    clean = torch.stack([signal(c['family'], c['seed'], config['side']) for c in cases])
    generator = torch.Generator().manual_seed(72)
    groups = {g: [] for g in ('crossed', 'angular', 'reserved', 'train_probe')}
    rows, losses, symmetries = [], [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(), freeze=freeze,
        freeze_sha256=digest(output/'freeze.json'), records=rows, groups=groups, losses=losses, symmetry_records=symmetries,
        parameter_count=sum(p.numel() for p in model.parameters()), status='incomplete', versions=dict(torch=torch.__version__),
        limits=['Already consulted procedural controls; no physical sensor/noise or photographic qualification.',
            'Stored parameter count does not count independent degrees of freedom after symmetry projection.',
            'CPU4threads128px; no full-definition or GPU resource conclusion.',
            'No early stopping, threshold changes, production integration or selection of best phase/seed.'])
    started = time.monotonic()
    try:
        for step, item in enumerate(schedule, start=1):
            ids = torch.randint(len(cases), (config['batch'],), generator=generator)
            if ids.tolist() != item['ids']:
                raise ValueError('Training ID RNG drift')
            target = clean[ids]
            for position in range(config['batch']):
                event = events.get((step, position))
                if event is not None:
                    truth = edge_image(event, config['side'])
                    if tensor_hash(truth) != event['target_sha256']:
                        raise ValueError('Training target changed')
                    target[position] = truth[0]
            pattern = torch.tensor([PATTERNS[p] for p in item['phases']], dtype=torch.int64)
            packed, sigma = noisy_observation(target, pattern, 0., 0., generator)
            for position, phase in enumerate(item['phases']):
                event = events.get((step, position))
                if event is not None and tensor_hash(packed[position:position+1]) != event['packed_sha256_by_phase'][phase]:
                    raise ValueError('Training CFA changed')
            model.train()
            optimizer.zero_grad(set_to_none=True)
            image = predict(model, packed/config['scale'], sigma, pattern, True, True)
            loss = (image[:, :, 48:-48, 48:-48]-target[:, :, 48:-48, 48:-48]/config['scale']).square().mean()
            loss.backward()
            if not torch.isfinite(loss) or not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
                raise ValueError('Nonfinite loss/gradient')
            optimizer.step()
            losses.append(float(loss.detach()))
            if step % 512 == 0:
                print(f'TRAIN {kind} seed{seed} {step}/{config["steps"]}', flush=True)
        model.eval()
        with torch.inference_mode():
            tasks = [('crossed', c, signal(c['family'], c['seed'], config['side'])[None], range(4))
                for c in proof['freeze']['evaluation_cases']]
            tasks += [('angular', c, angular_signal(c['normal_degrees'], c['offset'], config['side']), range(4))
                for c in proof['groups']['angular'] if c['phase_name'] == 'RGGB']
            tasks += [('reserved', c, edge_image(c, config['side']), range(4)) for c in proof['freeze']['reserved_edges']]
            tasks += [('train_probe', proof['freeze']['train_edge_events'][i], edge_image(proof['freeze']['train_edge_events'][i], config['side']),
                (proof['freeze']['train_edge_events'][i]['phase_index'],)) for i in proof['freeze']['train_probe_event_indices']]
            timed_inputs = []
            for group, case, truth, phases in tasks:
                for phase in phases:
                    pattern = torch.tensor([PATTERNS[phase]], dtype=torch.int64)
                    current = measure(model, truth, pattern, config['scale'])
                    row = dict(family=case.get('family', 'edge'), seed=case.get('seed'), phase_index=phase,
                        normal_degrees=case.get('normal_degrees'), offset=case.get('offset'), pattern=list(PATTERNS[phase]), **current)
                    groups[group].append(row)
                    if group == 'crossed' and row['pattern'] == case['pattern']:
                        rows.append(row)
                    if group == 'reserved':
                        packed = sample_rgb(truth, pattern)
                        source = predict(model, packed/config['scale'], torch.zeros_like(packed), pattern, True, True)*config['scale']
                        if phase == 0:
                            timed_inputs.append((packed, pattern))
                        for op in OPERATIONS:
                            tp, tags = transform_observation(packed, pattern, op)
                            image = transform(predict(model, tp/config['scale'], torch.zeros_like(tp), tags, True, True)*config['scale'], op, True)
                            score = difference(source, image)
                            if kind == 'equivariant' and score['max_absolute_difference'] > 2e-6:
                                raise ValueError('Structural equivariance tolerance exceeded')
                            if not torch.equal(sample_rgb(image, pattern), packed):
                                raise ValueError('Transformed observations changed')
                            symmetries.append(dict(seed=case['seed'], phase_index=phase, operation=list(op), **score))
            timings = []
            for packed, pattern in timed_inputs[:3]:
                predict(model, packed/config['scale'], torch.zeros_like(packed), pattern, True, True)
            for _ in range(7):
                tick = time.perf_counter()
                for packed, pattern in timed_inputs:
                    predict(model, packed/config['scale'], torch.zeros_like(packed), pattern, True, True)*config['scale']
                timings.append((time.perf_counter()-tick)/len(timed_inputs))
            result['timing'] = dict(seconds_per_128px_image=timings, median_seconds=statistics.median(timings), threads=4,
                scope='Seven32-input CPU runs; no I/O/metrics; observational comparison to earlier timings, not interleaved.')
        result.update(summaries(rows))
        result['qualification'] = qualifies(result, freeze['gates'])
        result['summaries'] = {g: summaries(r) for g, r in groups.items()}
        result['crossed_qualification'] = qualifies(dict(records=groups['crossed'], **summaries(groups['crossed'])), freeze['gates'])
        result['symmetry_summary'] = aggregate_differences(symmetries)
        torch.save(dict(state_dict=model.state_dict(), config=config, freeze_sha256=result['freeze_sha256']), output/'weights.pt')
        result['weights_sha256'] = digest(output/'weights.pt')
        if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
            raise ValueError('Study code changed')
        result['status'] = 'completed_closed_unet_pilot_not_photographic_qualification'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(dict(qualification=result['qualification'], crossed=result['summaries']['crossed']['summary'],
        reserved=result['summaries']['reserved']['summary'], symmetry=result['symmetry_summary'], timing=result['timing']), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--kind', choices=('closed', 'equivariant'), required=True)
    parser.add_argument('--seed', type=int, default=71)
    args = parser.parse_args()
    run(args.parent, args.output, args.kind, args.seed)
