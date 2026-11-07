"""Bounded random-data cost probe: never a quality-training result."""
import argparse
from contextlib import nullcontext
from datetime import datetime, timezone
import json
from pathlib import Path
import statistics
import sys
import time


def probe(config, root, guard):
    import torch
    from dru_cfa import DruCfaCorrector
    from load_control import YieldController
    from prepare_next_training import digest, read
    device = config['device']
    torch.set_num_threads(4 if device == 'cuda' else 8)
    torch.set_num_interop_threads(1)
    torch.manual_seed(811)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    if device == 'cuda':
        if not torch.cuda.is_available():
            raise RuntimeError('CUDA unavailable')
        free, _ = torch.cuda.mem_get_info()
        if free < 4*1024**3:
            raise RuntimeError('Insufficient free GPU memory')
        torch.cuda.set_per_process_memory_fraction(.5)
        torch.cuda.reset_peak_memory_stats()
    external = root/'build-patch_neural_restoration/third-party-drunet-v001'
    manifest = read(external/'manifest.json')
    for item in manifest['files']:
        if digest(external/item['path']) != item['sha256']:
            raise ValueError('Official source changed')
    sys.path.insert(0, str(external))
    from models.network_unet import UNetRes
    width = config['width']
    core = UNetRes(in_nc=8, out_nc=4, nc=[width, width*2, width*4, width*8], nb=4,
                  act_mode='R', downsample_mode='strideconv', upsample_mode='convtranspose')
    torch.nn.init.zeros_(core.m_tail.weight)
    model = DruCfaCorrector(core, checkpoint_stages=False).to(device)
    training = device == 'cuda'
    model.train(training)
    optimizer = torch.optim.Adam(model.parameters(), lr=1e-4) if training else None
    scaler = torch.amp.GradScaler('cuda', enabled=config['precision']=='amp-fp16')
    shape = (config['batch'], 4, config['side'], config['side'])
    clean = torch.rand(shape)  # CPU generation/H2D included in each timed update.
    sigma_cpu = torch.full(shape, .02)
    tags = torch.tensor([[0, 1, 3, 2]], device=device).expand(shape[0], -1)
    pressure = YieldController(dict(yield_memory_fraction=.85, yield_external_cpu_fraction=.80, pressure_seconds=15))
    times, norms, losses, skipped = [], [], [], 0
    for index in range(4+config['updates']):
        guard.check()
        if pressure.check():
            raise RuntimeError('Sustained host load: yielding')
        if device == 'cuda':
            torch.cuda.synchronize()
        started = time.perf_counter()
        target = clean.to(device)
        sigma = sigma_cpu.to(device)
        noisy = (clean+sigma_cpu*torch.randn_like(clean)).to(device)
        context = torch.autocast('cuda', dtype=torch.float16) if config['precision']=='amp-fp16' else nullcontext()
        if training:
            optimizer.zero_grad(set_to_none=True)
        with torch.set_grad_enabled(training), context:
            corrected = model.correct_observations(noisy, sigma, tags)
            loss = torch.nn.functional.l1_loss(corrected.float(), target)
        if not torch.isfinite(loss):
            raise ValueError('Nonfinite loss')
        if training:
            scaler.scale(loss).backward()
            scaler.unscale_(optimizer)
            norm = torch.nn.utils.clip_grad_norm_(model.parameters(), 1., error_if_nonfinite=True)
            previous_scale = scaler.get_scale()
            scaler.step(optimizer)
            scaler.update()
            skipped += int(scaler.get_scale() < previous_scale)
            if index >= 4:
                norms.append(float(norm))
        if device == 'cuda':
            torch.cuda.synchronize()
        elapsed = time.perf_counter()-started
        if index >= 4:
            times.append(elapsed)
            losses.append(float(loss))
    return dict(status='measured_engineering_only', configuration=config,
        parameters=sum(p.numel() for p in model.parameters()), torch_version=torch.__version__,
        device_name=torch.cuda.get_device_name() if device=='cuda' else 'CPU',
        cpu_threads=torch.get_num_threads(), optimizer='Adam 1e-4' if training else None,
        loss='unweighted L1, random patches; not a photographic result',
        effective_batch=shape[0], microbatches=1, activation_checkpointing=False,
        warmup_updates=4, seconds=times, median_seconds=statistics.median(times),
        gradient_norm_before_clipping=norms, clipping_events=sum(n>1 for n in norms),
        scaler_skips_including_warmup=skipped, loss_values=losses,
        peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated() if training else None,
        peak_cuda_reserved_bytes=torch.cuda.max_memory_reserved() if training else None,
        cpu_scope=None if training else 'Single 512x512 packed-CFA tile, not full-image opening or inference',
        source_revision=manifest['source_commit'], source_manifest_sha256=digest(external/'manifest.json'))


def main():
    from memory_budget import MemoryGuard, BudgetExceeded
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    config = json.loads(args.config)
    root = Path(__file__).resolve().parents[2]
    guard = MemoryGuard(hard_bytes=6*1024**3, soft_bytes=5*1024**3, system_fraction=.92)
    result = dict(configuration=config, started_at_utc=datetime.now(timezone.utc).isoformat())
    try:
        with guard:
            result.update(probe(config, root, guard))
    except BudgetExceeded as error:
        result.update(status='resource_stop', error=str(error))
    except ValueError as error:
        result.update(status='invalid_numeric', error=str(error))
    except Exception as error:
        result.update(status='unavailable_or_failed', error=type(error).__name__+': '+str(error))
    finally:
        result.update(memory=guard.stats(), finished_at_utc=datetime.now(timezone.utc).isoformat())
        with args.output.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)


if __name__ == '__main__':
    main()
