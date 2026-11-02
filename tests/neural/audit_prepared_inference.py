"""Verify one-time kernel preparation against frozen trained and confirmation outputs."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import subprocess
import time

import torch

from bayer_canonical import predict
from closed_moment_unet import build_model
from diagnose_train_learnability import digest, save
from joint_bayer_unet import sample_rgb
from prepare_closed_inference import prepare_inference
from run_balanced_moment_pilot import angular_signal
from run_diverse_moment_pilot import edge_image
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS, signal


def run(root, models, confirmation, source, output):
    if not all(p.resolve().is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in models+[confirmation, source, output]):
        raise ValueError('Use dedicated neural build')
    confirmed = json.loads((confirmation/'result.json').read_text(encoding='utf-8'))
    source_proof = json.loads((source/'result.json').read_text(encoding='utf-8'))
    if confirmed['status'] != 'completed_independent_seed_procedural_confirmation_no_training':
        raise ValueError('Completed confirmation required')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    now = datetime.now(timezone.utc)
    campaign = 'neural-prepared-inference-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True, exist_ok=False)
    result = dict(schema_version=1, data_version=1, campaign_id=campaign, measured_at_utc=now.isoformat(), models=[], status='incomplete',
        source_sha256=digest(source/'result.json'), confirmation_sha256=digest(confirmation/'result.json'),
        tools={n: digest(Path(__file__).with_name(n)) for n in ('audit_prepared_inference.py', 'prepare_closed_inference.py', 'closed_moment_unet.py')},
        git_sha=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        limits=['Identical small synthetic outputs, not production deployment or full-definition latency/memory validation.',
            'Preparation copy must be rebuilt if trained weights change; prepared object is inference-only.',
            'CPU4threads, seven interleaved runs of32images128px; no GPU measurement.'])
    try:
        for folder in models:
            proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
            config = proof['freeze']['config']
            if (proof['status'] != 'completed_closed_unet_pilot_not_photographic_qualification'
                    or config['model_kind'] != 'equivariant' or digest(folder/'weights.pt') != proof['weights_sha256']):
                raise ValueError('Completed unchanged equivariant model required')
            checkpoint = torch.load(folder/'weights.pt', map_location='cpu', weights_only=True)
            model = build_model('equivariant', config['width'])
            model.load_state_dict(checkpoint['state_dict'], strict=True)
            model.eval()
            model.requires_grad_(False)
            tick = time.perf_counter()
            prepared = prepare_inference(model)
            preparation_seconds = time.perf_counter()-tick
            confirmation_rows = next(m for m in confirmed['models'] if m['seed'] == config['seed'])
            tasks = []
            for row in proof['groups']['crossed']:
                tasks.append((row, signal(row['family'], row['seed'], config['side'])[None]))
            for row in proof['groups']['angular']:
                tasks.append((row, angular_signal(row['normal_degrees'], row['offset'], config['side'])))
            edge_lookup = {c['seed']: c for c in source_proof['freeze']['reserved_edges']}
            for row in proof['groups']['reserved']:
                tasks.append((row, edge_image(edge_lookup[row['seed']], config['side'])))
            for row in confirmation_rows['mixed_records']:
                tasks.append((row, signal(row['family'], row['seed'], config['side'])[None]))
            edge_lookup = {c['seed']: c for c in confirmed['plan']['edges']}
            for row in confirmation_rows['edge_records']:
                tasks.append((row, edge_image(edge_lookup[row['seed']], config['side'])))
            checked, timing_inputs = [], []
            with torch.inference_mode():
                for row, truth in tasks:
                    phase = row['phase_index']
                    tags = torch.tensor([PATTERNS[phase]])
                    packed = sample_rgb(truth, tags)
                    if tensor_hash(truth) != row['target_sha256'] or tensor_hash(packed) != row['packed_sha256']:
                        raise ValueError('Source replay differs')
                    image = predict(prepared, packed/config['scale'], torch.zeros_like(packed), tags, True, True)*config['scale']
                    if tensor_hash(image) != row['output_sha256'] or not torch.equal(sample_rgb(image, tags), packed):
                        raise ValueError('Prepared prediction is not bit-exact')
                    checked.append(dict(target_sha256=row['target_sha256'], packed_sha256=row['packed_sha256'], output_sha256=row['output_sha256']))
                    if len(timing_inputs) < 32:
                        timing_inputs.append((packed, tags))
                functions = dict(projected=model, prepared=prepared)
                samples = {name: [] for name in functions}
                for net in functions.values():
                    for _ in range(3):
                        p, tags = timing_inputs[0]
                        predict(net, p/config['scale'], torch.zeros_like(p), tags, True, True)
                for repetition in range(7):
                    order = tuple(functions) if repetition % 2 == 0 else tuple(reversed(functions))
                    for name in order:
                        tick = time.perf_counter()
                        for p, tags in timing_inputs:
                            predict(functions[name], p/config['scale'], torch.zeros_like(p), tags, True, True)*config['scale']
                        samples[name].append((time.perf_counter()-tick)/len(timing_inputs))
            if any(not torch.equal(v, model.state_dict()[n]) for n, v in checkpoint['state_dict'].items()):
                raise ValueError('Source state changed')
            result['models'].append(dict(seed=config['seed'], source_result_sha256=digest(folder/'result.json'), weights_sha256=proof['weights_sha256'],
                preparation_seconds=preparation_seconds, bit_exact_cases=len(checked), records=checked, timings=samples,
                median_seconds={name: statistics.median(v) for name, v in samples.items()}))
        if any(digest(Path(__file__).with_name(n)) != h for n, h in result['tools'].items()):
            raise ValueError('Study changed during audit')
        result['status'] = 'completed_bit_exact_prepared_inference_audit'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        save(output/'result.json', result)
        save(root/'docs/validation/raw'/f'{campaign}.json', result)
    print(json.dumps([dict(seed=m['seed'], bit_exact=m['bit_exact_cases'], preparation=m['preparation_seconds'], medians=m['median_seconds'])
        for m in result['models']], indent=2), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--models', type=Path, nargs=2, required=True)
    p.add_argument('--confirmation', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    run(Path(__file__).resolve().parents[2], args.models, args.confirmation, args.source, args.output)
