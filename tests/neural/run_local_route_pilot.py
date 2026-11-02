"""Paired fixed-budget local-route ablation, microbatch1 and enforced memory quota."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation, difference, aggregate_differences
from audit_coefficient_adaptation import coefficient_fields
from bayer_canonical import predict, canonicalize, spatial_reflect
from closed_moment_operator import interpolate_closed
from closed_moment_unet import build_model
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb, bilinear
from local_moment_unet import LocalMomentUNet
from memory_budget import MemoryGuard, BudgetExceeded
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import balanced_schedule
from run_memory_bounded_reconstruction import SampleStream
from run_synthetic_joint_pilot import tensor_hash, metrics, aggregate
from synthetic_bayer_data import PATTERNS, FAMILIES, signal


VARIANTS = ('full', 'bias_only', 'image_mean', 'core_mean', 'without_local')


def summary(rows, variant):
    return aggregate([dict(metrics=r['metrics'][variant]) for r in rows])


def gate_result(rows, gates):
    data = dict(records=rows, summary=summary(rows, 'full'),
        by_family={f: summary([r for r in rows if r['family'] == f], 'full') for f in FAMILIES})
    return qualifies(data, gates)


def run(parent, output, variant, seed=71):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    source = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    if source['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Frozen diversity data required')
    if isinstance(seed, bool) or not isinstance(seed, int) or seed < 0:
        raise ValueError('Nonnegative integer initialization seed required')
    config = dict(source['freeze']['config'], model_kind=variant, seed=seed, steps=2048, microbatch=1, accumulation=4)
    schedule, _ = balanced_schedule(source['freeze']['train_slots'], 2048, 4, config['side'])
    if tensor_hash(torch.tensor([[*e['ids'], *e['phases']] for e in schedule])) != source['freeze']['phase_schedule_sha256']:
        raise ValueError('Training schedule changed')
    tools = {**source['freeze']['tools'], **{n: digest(Path(__file__).with_name(n)) for n in
        ('closed_moment_unet.py', 'closed_moment_operator.py', 'local_moment_unet.py', 'memory_budget.py',
         'run_memory_bounded_reconstruction.py', 'audit_coefficient_adaptation.py', 'audit_bayer_symmetries.py', Path(__file__).name)}}
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Study dependency changed')
    now = datetime.now(timezone.utc)
    campaign = 'neural-local-route-'+variant+'-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze = dict(config=config, source_result_sha256=digest(parent/'result.json'), tools=tools,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        gates=source['freeze']['gates'], checkpoint='Final2048 only; no early stopping or threshold tuning.',
        policy='Declared initialization seed, fixed data/schedule/Adam.001/microbatch1x4, no best-seed selection.',
        local_features='Absolute central H/V slopes and H/V second differences, four values per physical plane; two equivariant convolutions then head.',
        evaluation='80consulted cases; full/bias/image-mean/core-mean/disabled-local frozen ablations; no independent confirmation here.')
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    records, losses, symmetries = [], [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(), freeze=freeze,
        freeze_sha256=digest(output/'freeze.json'), records=records, losses=losses, symmetry_records=symmetries, status='incomplete',
        limits=['Procedural reconstruction without noise, controls already consulted, no real-photo benefit claimed.',
            'Local branch adds2068parameters; useful diagnostic cannot isolate path length, descriptors and added capacity.',
            'Disabled branch is an ablation of jointly trained weights, not an independently optimized filter.',
            'No training on noise if20/80historical gates remain failed.'])
    guard = MemoryGuard()
    started = time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            torch.manual_seed(seed)
            model = LocalMomentUNet(config['width']) if variant == 'local' else build_model('equivariant', config['width'])
            result['parameter_count'] = sum(p.numel() for p in model.parameters())
            optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
            stream = SampleStream(source)
            for step, entry in enumerate(schedule, start=1):
                guard.check()
                model.train()
                optimizer.zero_grad(set_to_none=True)
                total = 0.
                for position, (index, phase) in enumerate(zip(entry['ids'], entry['phases'])):
                    guard.check()
                    target, packed, tags = stream.sample(step, position, index, phase)
                    image = predict(model, packed/config['scale'], torch.zeros_like(packed), tags, True, True)
                    loss = (image[:, :, 48:-48, 48:-48]-target[:, :, 48:-48, 48:-48]/config['scale']).square().mean()/4
                    if not torch.isfinite(loss):
                        raise ValueError('Nonfinite loss')
                    loss.backward()
                    total += float(loss.detach())
                    del image, loss, target, packed, tags
                if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
                    raise ValueError('Nonfinite gradient')
                optimizer.step()
                losses.append(total)
                if step % 512 == 0:
                    print(f'STEP {variant} {step}/2048 memory='+json.dumps(guard.stats()), flush=True)
            model.eval()
            optimizer.zero_grad(set_to_none=True)
            model.requires_grad_(False)
            captured = {}
            handles = [model.head.register_forward_hook(lambda module, args, value: captured.update(main=value.detach()))]
            if variant == 'local':
                handles.append(model.local_head.register_forward_hook(lambda module, args, value: captured.update(local=value.detach())))
            heads = [model.head]+([model.local_head] if variant == 'local' else [])
            bias = sum(h.bias*.5+h.bias[h.output_permutation]*.5 for h in heads)
            try:
                with torch.inference_mode():
                    for case in source['freeze']['evaluation_cases']:
                        target = signal(case['family'], case['seed'], config['side'])[None]
                        for phase, values in enumerate(PATTERNS):
                            guard.check()
                            tags = torch.tensor([values])
                            packed = sample_rgb(target, tags)
                            packed_hash = tensor_hash(packed)
                            image = predict(model, packed/config['scale'], torch.zeros_like(packed), tags, True, True)*config['scale']
                            main = captured.pop('main')
                            logits = main+captured.pop('local') if variant == 'local' else main
                            fields, stats = coefficient_fields(logits, bias)
                            fields['without_local'] = main.tanh()
                            cp, _, labels, flags = canonicalize(packed/config['scale'], torch.zeros_like(packed), tags)
                            base = bilinear(packed, tags)
                            measured, hashes = {}, {}
                            for name, coefficients in fields.items():
                                current = spatial_reflect(interpolate_closed(cp, labels, coefficients), flags)*config['scale']
                                if name == 'full' and not torch.equal(current, image):
                                    raise ValueError('Coefficient reconstruction differs')
                                if not torch.equal(sample_rgb(current, tags), packed):
                                    raise ValueError('Observed samples changed')
                                measured[name] = metrics(target, base, current)
                                hashes[name] = tensor_hash(current)
                            if tensor_hash(packed) != packed_hash:
                                raise ValueError('Source changed')
                            records.append(dict(family=case['family'], seed=case['seed'], phase_index=phase, pattern=list(values),
                                original_phase=list(values) == case['pattern'], observed_site_mse=0.,
                                target_sha256=tensor_hash(target), packed_sha256=packed_hash, output_sha256=hashes,
                                metrics=measured, coefficient_stats=stats))
                            if case['family'] == 'edge':
                                for op in OPERATIONS:
                                    guard.check()
                                    tp, labels = transform_observation(packed, tags, op)
                                    restored = transform(predict(model, tp/config['scale'], torch.zeros_like(tp), labels, True, True)*config['scale'], op, True)
                                    score = difference(image, restored)
                                    if score['max_absolute_difference'] > 2e-6:
                                        raise ValueError('Learned symmetry tolerance exceeded')
                                    symmetries.append(dict(seed=case['seed'], phase_index=phase, operation=list(op), **score))
                                captured.clear()
                    result['summary'] = {v: summary(records, v) for v in VARIANTS}
                    result['original20'] = {v: summary([r for r in records if r['original_phase']], v) for v in VARIANTS}
                    result['by_family'] = {f: {v: summary([r for r in records if r['family'] == f], v) for v in VARIANTS} for f in FAMILIES}
                    result['qualification20'] = gate_result([r for r in records if r['original_phase']], freeze['gates'])
                    result['qualification80'] = gate_result(records, freeze['gates'])
                    result['symmetry'] = aggregate_differences(symmetries)
            finally:
                for handle in handles:
                    handle.remove()
                captured.clear()
            torch.save(dict(state_dict=model.state_dict(), config=config, freeze_sha256=result['freeze_sha256']), output/'weights.pt')
            result['weights_sha256'] = digest(output/'weights.pt')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
                raise ValueError('Study changed during calculation')
            result['status'] = 'completed_local_route_pilot_not_noise_training'
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
    print(json.dumps(dict(summary={v: result['summary'][v]['mse_ratio'] for v in VARIANTS},
        original20={v: result['original20'][v]['mse_ratio'] for v in VARIANTS},
        qualification20=result['qualification20'], qualification80=result['qualification80'],
        memory=result['memory']), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--variant', choices=('baseline', 'local'), required=True)
    p.add_argument('--seed', type=int, default=71)
    a = p.parse_args()
    run(a.parent, a.output, a.variant, a.seed)
