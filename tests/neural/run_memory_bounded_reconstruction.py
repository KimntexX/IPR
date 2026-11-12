"""One predeclared longer reconstruction trial, microbatch1 and Windows hard quota."""
import argparse
from collections import OrderedDict
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import torch

from bayer_canonical import predict
from closed_moment_unet import build_model
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard, BudgetExceeded
from reconstruction_gates import qualifies
from run_balanced_moment_pilot import balanced_schedule, measure, summaries
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


class SampleStream:
    def __init__(self, proof):
        self.cases = proof['freeze']['train_slots']
        self.events = {(e['step'], e['batch_position']): e for e in proof['freeze']['train_edge_events']}
        self.side = proof['freeze']['config']['side']
        self.cache = OrderedDict()

    def sample(self, source_step, position, index, phase):
        event = self.events.get((source_step, position))
        if event is not None:
            target = edge_image(event, self.side)
            if tensor_hash(target) != event['target_sha256']:
                raise ValueError('Training edge changed')
        else:
            if index not in self.cache:
                case = self.cases[index]
                self.cache[index] = signal(case['family'], case['seed'], self.side)[None]
                if len(self.cache) > 4:
                    self.cache.popitem(last=False)
            self.cache.move_to_end(index)
            target = self.cache[index]
        tags = torch.tensor([PATTERNS[phase]])
        packed = sample_rgb(target, tags)
        if event is not None and tensor_hash(packed) != event['packed_sha256_by_phase'][phase]:
            raise ValueError('Training sensor samples changed')
        return target, packed, tags


def run(parent, output, preflight):
    root = Path(__file__).resolve().parents[2]
    parent, output = parent.resolve(), output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent, output)):
        raise ValueError('Use dedicated neural build')
    proof = json.loads((parent/'result.json').read_text(encoding='utf-8'))
    if proof['status'] != 'completed_diverse_contour_reconstruction_not_photographic_qualification':
        raise ValueError('Expected unchanged diversity dataset')
    config = dict(proof['freeze']['config'], seed=71, model_kind='equivariant',
        steps=32 if preflight else 8192, microbatch=1, accumulation=4)
    schedule, _ = balanced_schedule(proof['freeze']['train_slots'], 2048, 4, config['side'])
    if tensor_hash(torch.tensor([[*r['ids'], *r['phases']] for r in schedule])) != proof['freeze']['phase_schedule_sha256']:
        raise ValueError('Frozen schedule differs')
    now = datetime.now(timezone.utc)
    campaign = ('neural-memory-preflight-v001-' if preflight else 'neural-bounded-reconstruction-v001-')+now.strftime('%Y%m%dT%H%M%SZ')
    tools = {**proof['freeze']['tools'], **{n: digest(Path(__file__).with_name(n)) for n in
        ('closed_moment_unet.py', 'closed_moment_operator.py', 'memory_budget.py', Path(__file__).name)}}
    if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
        raise ValueError('Study code changed')
    freeze = dict(config=config, source_result_sha256=digest(parent/'result.json'), tools=tools,
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(), gates=proof['freeze']['gates'],
        policy='Noiseless gate closure only; repeat identical2048-step data schedule4times; one seed/final checkpoint, no threshold change.',
        memory_policy='Microbatch1, effective batch4, cache4images, no list of image tensors; OS2GiBcommit, soft1.25GiBprivate/RSS, system80percent.',
        preflight=preflight, limits=['Gradient accumulation preserves mathematical effective batch, not a promise of bitwise-identical Adam trajectory.',
            'Longer budget and microbatch execution both differ from original pilot; no causal claim about duration alone.',
            'No training on noisy data until prior20/80gates both pass; no real-photo/denoising qualification.'])
    output.mkdir(parents=True, exist_ok=False)
    save(output/'freeze.json', freeze)
    guard = MemoryGuard()
    rows, crossed, losses = [], [], []
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(),
        freeze=freeze, freeze_sha256=digest(output/'freeze.json'), status='incomplete', records=rows, crossed_records=crossed, losses=losses)
    started = time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            torch.manual_seed(71)
            model = build_model('equivariant', config['width'])
            optimizer = torch.optim.Adam(model.parameters(), lr=config['learning_rate'])
            stream = SampleStream(proof)
            for step in range(config['steps']):
                guard.check()
                source_step = step % len(schedule)+1
                entry = schedule[source_step-1]
                model.train()
                optimizer.zero_grad(set_to_none=True)
                total_loss = 0.
                for position, (index, phase) in enumerate(zip(entry['ids'], entry['phases'])):
                    guard.check()
                    target, packed, tags = stream.sample(source_step, position, index, phase)
                    image = predict(model, packed/config['scale'], torch.zeros_like(packed), tags, True, True)
                    loss = (image[:, :, 48:-48, 48:-48]-target[:, :, 48:-48, 48:-48]/config['scale']).square().mean()/4
                    if not torch.isfinite(loss):
                        raise ValueError('Nonfinite loss')
                    loss.backward()
                    total_loss += float(loss.detach())
                    del loss, image, target, packed, tags
                if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
                    raise ValueError('Nonfinite gradient')
                optimizer.step()
                losses.append(total_loss)
                if (step+1) % 512 == 0:
                    print('STEP '+str(step+1)+' memory='+json.dumps(guard.stats()), flush=True)
            optimizer.zero_grad(set_to_none=True)
            model.eval()
            if not preflight:
                with torch.inference_mode():
                    for case in proof['freeze']['evaluation_cases']:
                        guard.check()
                        target = signal(case['family'], case['seed'], config['side'])[None]
                        for phase, values in enumerate(PATTERNS):
                            guard.check()
                            current = measure(model, target, torch.tensor([values]), config['scale'])
                            record = dict(family=case['family'], seed=case['seed'], phase_index=phase, pattern=list(values), **current)
                            crossed.append(record)
                            if record['pattern'] == case['pattern']:
                                rows.append(record)
                result.update(summaries(rows))
                result['qualification'] = qualifies(result, freeze['gates'])
                result['crossed'] = summaries(crossed)
                result['crossed_qualification'] = qualifies(dict(records=crossed, **summaries(crossed)), freeze['gates'])
                torch.save(dict(state_dict=model.state_dict(), config=config, freeze_sha256=result['freeze_sha256']), output/'weights.pt')
                result['weights_sha256'] = digest(output/'weights.pt')
            guard.check()
            if any(digest(Path(__file__).with_name(n)) != h for n, h in tools.items()):
                raise ValueError('Study changed during calculation')
            result['status'] = 'completed_memory_preflight_no_quality_evaluation' if preflight else 'completed_memory_bounded_reconstruction_not_noise_training'
    except (BudgetExceeded, MemoryError) as error:
        result['status'] = 'aborted_for_memory_budget'
        result['error'] = str(error)
        raise
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        result['memory'] = guard.stats()
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps(dict(status=result['status'], memory=result['memory'], qualification=result.get('qualification'),
        crossed=result.get('crossed_qualification')), indent=2), flush=True)
    print('COMPLETE '+campaign, flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--preflight', action='store_true')
    a = p.parse_args()
    run(a.parent, a.output, a.preflight)
