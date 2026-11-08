"""Thirty-minute natural-content learning preflight, not sensor-noise qualification."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'


def normalized_patch(mosaic, pattern, black, white):
    import numpy as np
    if mosaic.ndim != 2 or mosaic.shape[0] != mosaic.shape[1] or mosaic.shape[0] % 2:
        raise ValueError('Even square Bayer patch required')
    pattern = np.asarray(pattern)
    if pattern.shape != (2, 2) or sorted(pattern.ravel().tolist()) != [0, 1, 2, 3]:
        raise ValueError('Four distinct physical Bayer labels required')
    red = np.argwhere(pattern == 0)[0]
    blue = np.argwhere(pattern == 2)[0]
    if not np.array_equal(blue, 1-red) or not np.isfinite(white) or white <= max(black):
        raise ValueError('Opposite R/B corners and valid white/black required')
    value = mosaic.astype(np.float32)
    for y in range(2):
        for x in range(2):
            b = float(black[int(pattern[y, x])])
            value[y::2, x::2] = (value[y::2, x::2]-b)/(white-b)
    if red[0]:
        value = value[::-1, :]
    if red[1]:
        value = value[:, ::-1]
    result = np.stack([value[y::2, x::2] for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))])
    if not np.isfinite(result).all():
        raise ValueError('Nonfinite normalized CFA')
    return result.astype('<f4')


def prepare(output):
    from datetime import timedelta
    inventory = read(BUILD/'training-preparation-v001/natural-inventory.json')
    selected = [row for row in inventory['sources'] if row['accessible']]
    if len(selected) != 4:
        raise ValueError('Expected the four previously inspected available ISO100 sources')
    output.mkdir(exist_ok=False)
    tools = ['natural_cfa_preflight.py', 'dru_cfa.py', 'dru_checkpoint.py',
             'prepare_next_training.py', 'memory_budget.py', 'load_control.py', 'campaign_artifacts.py']
    plan = dict(schema_version=1, created_at_utc=datetime.now(timezone.utc).isoformat(),
        deadline_utc=(datetime.now(timezone.utc)+timedelta(minutes=30)).isoformat(),
        seeds=[71, 73], data_seed=8001, steps=1024, batch=8, packed_side=256,
        parameters=8162688, width=32, precision='fp32', learning_rate=.0001,
        objective='unweighted L1', sigma_levels=[.005, .01, .02, .04],
        activation_checkpointing=True,
        sources=[dict(**row, role='training' if index < 2 else 'development_known_not_final_test')
                 for index, row in enumerate(selected)],
        tools={name:digest(ROOT/'tests/neural'/name) for name in tools},
        limits=['Previously inspected ISO100 single-frame proxies, not noise-free targets',
                'Fixed 3x3 geometric crops, no score-driven selection, no focus qualification',
                'Source-level provisional development split, not a qualified scene/session final test',
                'Read-only rawpy unpack, metadata black/white normalization, no IPR calibration claim',
                'Signed normalized sensor CFA, no WB, demosaic, DCP or LCP',
                'Independent additive constant Gaussian noise, not calibrated ISO noise or CRX simulation',
                'Known constant sigma input has no clean-dependent spatial map',
                'Same data/noise schedule across model seeds, development evaluated at fixed steps',
                'No prospective reserve reopened, no procedural recipe extension, no production change'])
    write(output/'plan.json', plan)


def extract(output, guard):
    import numpy as np
    import rawpy
    plan = read(output/'plan.json')
    arrays, entries = [], []
    for row in plan['sources']:
        guard.check()
        path = Path(row['source'])
        if digest(path) != row['expected_sha256']:
            raise ValueError('Original source hash differs: '+str(path))
        with rawpy.imread(str(path)) as raw:
            mosaic = raw.raw_image_visible
            colours = raw.raw_colors_visible
            pattern = colours[:2, :2].copy()
            black, white = list(raw.black_level_per_channel), float(raw.white_level)
            height, width = mosaic.shape
            for fy in (.2, .5, .8):
                for fx in (.2, .5, .8):
                    y, x = (int(fy*height)-256)//2*2, (int(fx*width)-256)//2*2
                    tile = mosaic[y:y+512, x:x+512].copy()
                    if tile.shape != (512, 512):
                        raise ValueError('Crop outside visible CFA')
                    normalized = normalized_patch(tile, pattern, black, white)
                    arrays.append(normalized)
                    entries.append(dict(source=str(path), source_sha256=row['expected_sha256'], role=row['role'],
                        crop_xy=[x, y], pattern=pattern.tolist(), black=black, white=white,
                        normalized_sha256=hashlib.sha256(normalized.tobytes()).hexdigest(),
                        negative_fraction=float((normalized < 0).mean()),
                        at_or_above_white_fraction=float((normalized >= 1).mean()),
                        focus_qualified=False, iso_from_prior_inventory=row['iso']))
            del colours, mosaic
    data = np.stack(arrays)
    np.save(output/'targets.npy', data, allow_pickle=False)
    write(output/'data.json', dict(status='extracted_natural_CFA_proxies_not_clean_truth',
        entries=entries, dataset_sha256=digest(output/'targets.npy'), shape=list(data.shape),
        rawpy_version=rawpy.__version__, libraw_version=list(rawpy.libraw_version)))


def train(output, seed, guard):
    import numpy as np
    import torch
    from dru_cfa import DruCfaCorrector
    from dru_checkpoint import fingerprint
    from load_control import YieldController
    plan, data = read(output/'plan.json'), read(output/'data.json')
    if digest(output/'targets.npy') != data['dataset_sha256']:
        raise ValueError('Natural targets changed')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0] < 4*1024**3:
        raise RuntimeError('GPU unavailable or busy')
    torch.cuda.set_per_process_memory_fraction(.5)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    external = BUILD/'third-party-drunet-v001'
    manifest = read(external/'manifest.json')
    for item in manifest['files']:
        if digest(external/item['path']) != item['sha256']:
            raise ValueError('Official architecture source changed')
    sys.path.insert(0, str(external))
    from models.network_unet import UNetRes
    torch.manual_seed(seed)
    core = UNetRes(in_nc=8, out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                  downsample_mode='strideconv', upsample_mode='convtranspose')
    torch.nn.init.zeros_(core.m_tail.weight)
    model = DruCfaCorrector(core, checkpoint_stages=plan['activation_checkpointing']).cuda()
    if sum(p.numel() for p in model.parameters()) != plan['parameters']:
        raise ValueError('Unexpected parameter count')
    optimizer = torch.optim.Adam(model.parameters(), lr=plan['learning_rate'])
    arrays = np.load(output/'targets.npy', allow_pickle=False)
    training = torch.from_numpy(arrays[[i for i,e in enumerate(data['entries']) if e['role']=='training']]).cuda()
    dev = torch.from_numpy(arrays[[i for i,e in enumerate(data['entries']) if e['role']!='training']]).cuda()
    levels = torch.tensor(plan['sigma_levels'], device='cuda')
    rng = torch.Generator(device='cuda').manual_seed(plan['data_seed'])
    folder = output/f'seed{seed}'
    folder.mkdir(exist_ok=False)
    context = dict(plan_sha256=digest(output/'plan.json'), seed=seed, torch_version=str(torch.__version__))
    result = dict(status='training', seed=seed, started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan_sha256=context['plan_sha256'], initial_model_fingerprint=fingerprint(model.state_dict()),
        losses=[], update_seconds=[], controls=[], completed_steps=0)
    completed = 0
    load = YieldController(dict(yield_memory_fraction=.85, yield_external_cpu_fraction=.80, pressure_seconds=15))

    def check():
        guard.check()
        if time.time() >= datetime.fromisoformat(plan['deadline_utc']).timestamp() or (output/'STOP').exists():
            raise InterruptedError('Deadline or STOP: save last complete update')
        if load.check():
            raise InterruptedError('Sustained host pressure: save last complete update')

    def checkpoint(name):
        temporary = folder/(name+'.tmp')
        try:
            torch.save(dict(schema_version=1, context=context, completed_steps=completed,
                batch=plan['batch'], observations_drawn=completed*plan['batch'],
                losses=result['losses'], model_state=model.state_dict(), optimizer_state=optimizer.state_dict(),
                data_rng=rng.get_state(), cpu_rng=torch.get_rng_state(), cuda_rng=torch.cuda.get_rng_state_all()),
                temporary)
            temporary.replace(folder/name)
        finally:
            temporary.unlink(missing_ok=True)

    def control(step):
        model.eval()
        rows = []
        with torch.inference_mode():
            for index, target in enumerate(dev):
                for level_index, level in enumerate(plan['sigma_levels']):
                    check()
                    g = torch.Generator(device='cuda').manual_seed(9001000+index*4+level_index)
                    clean = target.unsqueeze(0)
                    noise = torch.randn(clean.shape, device='cuda', generator=g)*level
                    noisy = clean+noise
                    sigma = torch.full_like(noisy, level)
                    tags = torch.tensor([[0, 1, 3, 2]], device='cuda')
                    corrected = model.correct_observations(noisy, sigma, tags)
                    # Avoid the padded boundary in diagnostics, without claiming a full RF halo.
                    error = (corrected-clean)[..., 32:-32, 32:-32]
                    original = noise[..., 32:-32, 32:-32]
                    mse, before = float(error.square().mean()), float(original.square().mean())
                    rows.append(dict(target_index=index, sigma=level, corrected_mse=mse, observed_mse=before,
                        psnr_gain_db=10*float(np.log10(before/max(mse, 1e-30))),
                        mean_error_by_plane=error.mean(dim=(-2,-1))[0].tolist()))
        result['controls'].append(dict(step=step, rows=rows))
        model.train()
        # Validation uses batch1; release its cached workspace before batch8 training.
        torch.cuda.empty_cache()
        write(folder/'progress.json', result)

    try:
        control(0)
        for step in range(1, plan['steps']+1):
            check()
            optimizer.zero_grad(set_to_none=True)
            torch.cuda.synchronize()
            started = time.perf_counter()
            ids = torch.randint(len(training), (plan['batch'],), device='cuda', generator=rng)
            target = training[ids]
            level_ids = torch.randint(len(levels), (plan['batch'],), device='cuda', generator=rng)
            sigma = levels[level_ids, None, None, None].expand_as(target)
            noisy = target+sigma*torch.randn(target.shape, device='cuda', generator=rng)
            tags = torch.tensor([[0, 1, 3, 2]], device='cuda').expand(plan['batch'], -1)
            prediction = model.correct_observations(noisy, sigma, tags)
            loss = torch.nn.functional.l1_loss(prediction, target)
            if not torch.isfinite(loss):
                raise ValueError('Nonfinite L1 loss')
            loss.backward()
            norm = torch.nn.utils.clip_grad_norm_(model.parameters(), 1., error_if_nonfinite=True)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
            torch.cuda.synchronize()
            result['update_seconds'].append(time.perf_counter()-started)
            result['losses'].append(dict(l1=float(loss.detach()), gradient_norm=float(norm)))
            completed = step
            result['completed_steps'] = step
            if step % 32 == 0:
                write(folder/'progress.json', result)
            if step in (256, 512, 1024):
                control(step)
            if step == 512:
                checkpoint('step512.pt')
        result['status'] = 'completed_1024_natural_content_development_preflight'
    except Exception as error:
        result.update(status='stopped', error=type(error).__name__+': '+str(error))
        raise
    finally:
        optimizer.zero_grad(set_to_none=True)
        checkpoint('resume.pt')
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(), memory=guard.stats(),
            final_checkpoint_sha256=digest(folder/'resume.pt'), checkpoint_includes_actual_batch_and_data_rng=True,
            activation_checkpointing=plan['activation_checkpointing'],
            peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated(),
            peak_cuda_reserved_bytes=torch.cuda.max_memory_reserved())
        write(folder/'progress.json', result)


def worker(output, mode, seed):
    from memory_budget import MemoryGuard
    with MemoryGuard(hard_bytes=6*1024**3, soft_bytes=5*1024**3, system_fraction=.92) as guard:
        if mode == 'extract':
            extract(output, guard)
        else:
            train(output, seed, guard)


def run(output):
    from memory_budget import snapshot
    plan = read(output/'plan.json')
    deadline = datetime.fromisoformat(plan['deadline_utc']).timestamp()
    result = dict(status='admission', pid=__import__('os').getpid(), plan=plan, stages=[],
                  started_at_utc=datetime.now(timezone.utc).isoformat())
    def check():
        if time.time() >= deadline or (output/'STOP').exists():
            raise InterruptedError('Thirty-minute campaign limit or STOP')
        for name, expected in plan['tools'].items():
            if digest(ROOT/'tests/neural'/name) != expected:
                raise ValueError('Frozen preflight code changed')
    try:
        stable = None
        while True:
            check()
            if snapshot()['system_used_fraction'] < .7:
                stable = stable or time.monotonic()
                if time.monotonic()-stable >= 30:
                    break
            else:
                stable = None
            write(output/'progress.json', result)
            time.sleep(5)
        for mode, seed in (('extract', 0), ('train', 71), ('train', 73)):
            check()
            python = ('C:/Users/victo/AppData/Local/Python/pythoncore-3.14-64/python.exe' if mode == 'extract'
                      else str(ROOT.parent/'venv-neural-cuda/Scripts/python.exe'))
            name = f'{mode}-{seed}'
            args = [python, str(Path(__file__).resolve()), '--output', str(output), '--mode', mode, '--seed', str(seed)]
            row = dict(name=name, command=args)
            result['stages'].append(row)
            result['status'] = name
            with (output/f'{name}.log').open('x', encoding='utf-8') as log:
                child = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
                row['pid'] = child.pid
                write(output/'progress.json', result)
                try:
                    while child.poll() is None:
                        check()
                        try:
                            child.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            pass
                finally:
                    if child.poll() is None:
                        (output/'STOP').write_text('Complete update and save checkpoint.\n', encoding='utf-8')
                        try:
                            child.wait(timeout=45)
                        except subprocess.TimeoutExpired:
                            subprocess.run(['taskkill', '/PID', str(child.pid), '/T', '/F'],
                                           capture_output=True, timeout=20, check=False)
                            if child.poll() is None:
                                child.kill()
                    child.wait(timeout=15)
                row['returncode'] = child.returncode
            if child.returncode:
                raise RuntimeError('Preflight worker failed: '+name)
        result['status'] = 'completed_both_seeds_development_only'
    except Exception as error:
        result.update(status='stopped', error=str(error))
    finally:
        result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        result['data'] = read(output/'data.json') if (output/'data.json').exists() else None
        result['measurements'] = [read(output/f'seed{s}/progress.json') for s in plan['seeds']
                                  if (output/f'seed{s}/progress.json').exists()]
        write(output/'progress.json', result)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-natural-preflight-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('prepare', 'run', 'extract', 'train'), required=True)
    parser.add_argument('--seed', type=int, default=71)
    args = parser.parse_args()
    if args.mode == 'prepare':
        prepare(args.output.resolve())
    elif args.mode == 'run':
        run(args.output.resolve())
    else:
        worker(args.output.resolve(), args.mode, args.seed)
