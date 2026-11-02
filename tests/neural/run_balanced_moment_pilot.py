"""Single ablation: cycle all Bayer phases per training content, no other fit change."""
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
from joint_bayer_unet import sample_rgb, bilinear
from moment_bayer_unet import model_from_config
from reconstruction_gates import qualifies
from run_synthetic_joint_pilot import metrics, aggregate, tensor_hash
from synthetic_bayer_data import PATTERNS, FAMILIES, signal, noisy_observation


def balanced_schedule(cases, steps, batch, side):
    """Retain original ID/noise RNG draws; cycle phases independently per content."""
    generator = torch.Generator().manual_seed(72)
    counts = torch.zeros(len(cases), 4, dtype=torch.int64)
    schedule = []
    for _ in range(steps):
        ids = torch.randint(len(cases), (batch,), generator=generator)
        phases = []
        for index in ids.tolist():
            first = PATTERNS.index(tuple(cases[index]['pattern']))
            phase = (first+int(counts[index].sum())) % 4
            counts[index, phase] += 1
            phases.append(phase)
        # The original zero-noise helper draws these values even when sigma=0.
        torch.randn(batch, 4, side//2, side//2, generator=generator)
        schedule.append(dict(ids=ids.tolist(), phases=phases))
    if ((counts.max(dim=1).values-counts.min(dim=1).values) > 1).any():
        raise ValueError('Per-content phase balance violated')
    return schedule, counts


def angular_signal(angle_degrees, offset, side=128):
    if not 0 <= angle_degrees < 180 or side < 32 or side % 2 or not math.isfinite(offset):
        raise ValueError('Finite offset, normal angle [0,180), even side>=32 required')
    y, x = torch.meshgrid(torch.linspace(-1, 1, side), torch.linspace(-1, 1, side), indexing='ij')
    theta = angle_degrees*math.pi/180
    region = (x*math.cos(theta)+y*math.sin(theta) > offset)[None]
    low = torch.tensor([.12, .32, .55])[:, None, None]
    high = torch.tensor([.80, .54, .18])[:, None, None]
    return torch.where(region, high, low).float()[None]


def measure(model, target, pattern, scale):
    packed = sample_rgb(target, pattern)
    before = tensor_hash(packed)
    output = predict(model, packed/scale, torch.zeros_like(packed), pattern,
        exact_observations=True, canonical_input=True)*scale
    if tensor_hash(packed) != before or not torch.equal(sample_rgb(output, pattern), packed):
        raise ValueError('Source or exact observation changed')
    return dict(target_sha256=tensor_hash(target), packed_sha256=before,
        output_sha256=tensor_hash(output), metrics=metrics(target, bilinear(packed, pattern), output),
        observed_site_mse=0.)


def summaries(rows):
    return dict(summary=aggregate(rows), by_family={f: aggregate([r for r in rows if r['family'] == f])
        for f in FAMILIES if any(r['family'] == f for r in rows)})


def run(parent, phase_audit, output):
    root = Path(__file__).resolve().parents[2]
    parent, phase_audit, output = [p.resolve() for p in (parent, phase_audit, output)]
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, phase_audit, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    phase_proof = json.loads((phase_audit/'result.json').read_text(encoding='utf-8'))
    weights = parent/'synthetic-joint-weights.pt'
    config = dict(proof['freeze']['config'])
    if (proof['status'] != 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
            or config['model_kind'] != 'moment' or not config['canonical_input'] or not config['exact_observations']
            or phase_proof['status'] != 'completed_crossed_bayer_phase_audit_no_training'
            or phase_proof['freeze']['parent']['result_sha256'] != digest(parent/'result.json')
            or digest(weights) != proof['weights_sha256']):
        raise ValueError('Expected completed fixed moment reference')
    for name, expected in proof['freeze']['tools'].items():
        if digest(Path(__file__).with_name(name)) != expected:
            raise ValueError('Changed parent implementation: '+name)
    cases = proof['freeze']['train_cases']
    tests = proof['freeze']['evaluation_cases']
    if len(cases) != 40 or len(tests) != 20 or config['batch'] != 4:
        raise ValueError('Expected original 40/20 contents and batch4')
    schedule, counts = balanced_schedule(cases, config['steps'], config['batch'], config['side'])
    angular = [dict(family='edge', normal_degrees=11.25+22.5*i, offset=offset)
        for i in range(8) for offset in (-.08, .08)]
    # Freeze all new truths and mosaics BEFORE constructing/training the model.
    for case in angular:
        truth = angular_signal(case['normal_degrees'], case['offset'], config['side'])
        case['target_sha256'] = tensor_hash(truth)
        case['packed_sha256_by_phase'] = [tensor_hash(sample_rgb(truth, torch.tensor([p]))) for p in PATTERNS]
    now = datetime.now(timezone.utc)
    campaign = 'neural-balanced-moment-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        config=config, train_cases=cases, evaluation_cases=tests, angular_cases=angular,
        training_stage='moment-reconstruction', gates=proof['freeze']['gates'],
        phase_schedule_sha256=tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule])),
        phase_counts_by_content=counts.tolist(), phase_counts_total=counts.sum(dim=0).tolist(),
        phase_policy='Per-content cycling: exposures differ by at most one; original ID/noise RNG draws retained.',
        reference=dict(result_sha256=digest(parent/'result.json'), weights_sha256=digest(weights),
            phase_audit_sha256=digest(phase_audit/'result.json')),
        tools={**proof['freeze']['tools'], Path(__file__).name: digest(Path(__file__))},
        selection='One final checkpoint after2048 steps; no angular control used for optimization or selection.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    torch.manual_seed(config['seed'])
    model = model_from_config(config)
    optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
    reference = model_from_config(config)
    checkpoint = torch.load(weights, map_location='cpu', weights_only=True)
    if checkpoint['freeze_sha256'] != proof['freeze_sha256']:
        raise ValueError('Reference checkpoint provenance differs')
    reference.load_state_dict(checkpoint['state_dict'], strict=True)
    reference.requires_grad_(False)
    reference.eval()
    clean = torch.stack([signal(c['family'], c['seed'], config['side']) for c in cases])
    generator = torch.Generator().manual_seed(72)
    rows, crossed, angular_rows, losses = [], [], [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, freeze_sha256=digest(output/'freeze.json'), status='incomplete', records=rows,
        crossed_records=crossed, angular_records=angular_rows, losses=losses,
        parameter_count=sum(p.numel() for p in model.parameters()), threads_intra=4,
        versions=dict(torch=torch.__version__), limits=[
            'Only phase exposure changes; pixel targets after canonical reflection therefore differ from prior training.',
            'Same already consulted procedural train/control families; no photographic qualification.',
            'New angular targets are frozen before training, share a single fixed colour pair and are not natural scenes.',
            'No noise training, sensor calibration, new model selection, production hook or default change.'])
    started = time.monotonic()
    try:
        for step, item in enumerate(schedule, start=1):
            ids = torch.randint(len(cases), (config['batch'],), generator=generator)
            if ids.tolist() != item['ids']:
                raise ValueError('Training ID draw drift')
            target = clean[ids]
            pattern = torch.tensor([PATTERNS[p] for p in item['phases']], dtype=torch.int64)
            noisy, sigma = noisy_observation(target, pattern, 0., 0., generator)
            model.train()
            optimizer.zero_grad(set_to_none=True)
            image = predict(model, noisy/config['scale'], sigma, pattern,
                exact_observations=True, canonical_input=True)
            loss = (image[:, :, 48:-48, 48:-48]-target[:, :, 48:-48, 48:-48]/config['scale']).square().mean()
            loss.backward()
            if not torch.isfinite(loss) or not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
                raise ValueError('Nonfinite training loss/gradient')
            optimizer.step()
            losses.append(float(loss.detach()))
            if step % 512 == 0:
                print(f'TRAIN {step}/{config["steps"]}', flush=True)
        model.eval()
        old = {(r['family'], r['seed'], tuple(r['pattern'])): r for r in phase_proof['records']}
        with torch.inference_mode():
            for case in tests:
                truth = signal(case['family'], case['seed'], config['side'])[None]
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    current = measure(model, truth, pattern, config['scale'])
                    previous = measure(reference, truth, pattern, config['scale'])
                    expected = old[(case['family'], case['seed'], tuple(values))]
                    if any(previous[k] != expected[k] for k in ('target_sha256', 'packed_sha256', 'output_sha256')):
                        raise ValueError('Reference crossed replay differs')
                    row = dict(family=case['family'], seed=case['seed'], pattern=list(values),
                        phase_name=PHASE_NAMES[phase], **current, reference_metrics=previous['metrics'])
                    crossed.append(row)
                    if list(values) == case['pattern']:
                        rows.append(dict(**case, **current))
            for case in angular:
                truth = angular_signal(case['normal_degrees'], case['offset'], config['side'])
                if tensor_hash(truth) != case['target_sha256']:
                    raise ValueError('Angular truth changed')
                for phase, values in enumerate(PATTERNS):
                    pattern = torch.tensor([values], dtype=torch.int64)
                    current = measure(model, truth, pattern, config['scale'])
                    previous = measure(reference, truth, pattern, config['scale'])
                    if current['packed_sha256'] != case['packed_sha256_by_phase'][phase]:
                        raise ValueError('Angular observation changed')
                    angular_rows.append(dict(family='edge', normal_degrees=case['normal_degrees'], offset=case['offset'],
                        phase_name=PHASE_NAMES[phase], pattern=list(values), **current, reference_metrics=previous['metrics']))
        result.update(summaries(rows))
        result['qualification'] = qualifies(result, freeze['gates'])
        for name, records in (('crossed', crossed), ('angular', angular_rows)):
            result[name] = summaries(records)
            result[name]['reference'] = summaries([dict(family=r['family'], metrics=r['reference_metrics']) for r in records])
            result[name]['by_phase'] = {p: aggregate([r for r in records if r['phase_name'] == p]) for p in PHASE_NAMES}
        result['crossed_qualification'] = qualifies(dict(records=crossed, **summaries(crossed)), freeze['gates'])
        result['angular_by_normal'] = {str(11.25+22.5*i): dict(
            candidate=aggregate([r for r in angular_rows if r['normal_degrees'] == 11.25+22.5*i]),
            reference=aggregate([dict(metrics=r['reference_metrics']) for r in angular_rows if r['normal_degrees'] == 11.25+22.5*i]))
            for i in range(8)}
        torch.save(dict(state_dict=model.state_dict(), config=config, freeze_sha256=result['freeze_sha256']),
            output/'synthetic-joint-weights.pt')
        result['weights_sha256'] = digest(output/'synthetic-joint-weights.pt')
        if digest(weights) != freeze['reference']['weights_sha256'] or any(
                digest(Path(__file__).with_name(n)) != h for n, h in freeze['tools'].items()):
            raise ValueError('Reference or implementation changed')
        result['status'] = 'completed_synthetic_reconstruction_stage_not_photographic_qualification'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(dict(summary=result['summary'], qualification=result['qualification'],
        crossed=result['crossed']['summary'], angular=result['angular']['summary']), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--phase-audit', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.parent, args.phase_audit, args.output)
