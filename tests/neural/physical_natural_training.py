"""Two-seed natural CFA training under the measured physical noise model (plan A).

Clean proxies are ISO100 R6 III mosaics in the archived native signed domain.
Noise is synthesised in sensor DN per ISO: Poisson photons at the measured
shot slope, the frozen nearest-neighbour photon covariance, empirical dark
frames (read noise, rows, columns, fixed pattern) and ADC clipping. The sigma
map comes from the same estimator on the noisy input in training, controls and
inference. Development only: not a final test, a calibrated comparator or the
+0.5 dB criterion.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
NATURAL = BUILD/'native-normalized-natural-v002'
BANK = ROOT/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
COVARIANCE = BUILD/'photon-covariance-fit-v001/result.json'
DARK_GROUPS = (BUILD/'remaining-dark-component-v001/progress.json', BUILD/'new-dark-qualification-v001/progress.json')
TRAINING_ISO = (3200, 6400, 16000, 51200)
SOURCE_BLACK, SOURCE_WHITE = 512, 13995
TARGET_BLACK, TARGET_WHITE, ADC_MAX = 2048, 14351, 16383
TILE = 512
CANONICAL_TAGS = (0, 1, 3, 2)


def grid_origins(height, width, tile=TILE, margin=32):
    """Even-aligned, non-overlapping tile origins inside the visible mosaic."""
    if height < tile+2*margin or width < tile+2*margin or tile % 2 or margin % 2:
        raise ValueError('Mosaic too small or odd tile geometry')
    ys = range(margin, height-margin-tile+1, tile)
    xs = range(margin, width-margin-tile+1, tile)
    return [(y, x) for y in ys for x in xs]


def pack_rggb(mosaic):
    """Raster 2x2 positions in canonical R/G1/G2/B order; RGGB phase required upstream."""
    import numpy as np
    if mosaic.ndim != 2 or mosaic.shape[0] % 2 or mosaic.shape[1] % 2:
        raise ValueError('Even 2-D mosaic required')
    return np.stack([mosaic[y::2, x::2] for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))])


def source_normalized(mosaic, divisor):
    """Archived native rounding: float32 DN minus black, times float32(1/divisor)."""
    import numpy as np
    values = pack_rggb(mosaic).astype(np.float32)-np.float32(SOURCE_BLACK)
    values *= np.float32(1/divisor)
    return values.astype('<f4')


def plane_pedestals(mosaic):
    """Mean of each raster Bayer position over the whole dark frame."""
    import numpy as np
    return np.array([float(mosaic[y::2, x::2].mean(dtype=np.float64)) for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))])


def noise_parameters(bank, covariance, isos=TRAINING_ISO):
    """Per ISO and canonical plane: shot slope, temporal read variance (DN) and photon covariance."""
    fits = {(r['iso'], r['plane']): r for r in bank['fits']}
    gammas = {(r['iso'], r['plane']): r for r in covariance['coefficients']}
    rows = []
    for iso in isos:
        for plane in range(4):
            fit = fits[(iso, plane)]
            slope = fit['fit'].get('shot_slope_dn')
            gamma = gammas[(iso, plane)]
            if slope is None or slope <= 0 or not gamma['positive_spectrum']:
                raise ValueError(f'No measured positive model at ISO {iso}, plane {plane}')
            rows.append(dict(iso=iso, plane=plane, shot_slope_dn=float(slope),
                read_variance_dn2=float(fit['reference_dark_variance_dn2']),
                gamma_x=float(gamma['gamma_x']), gamma_y=float(gamma['gamma_y'])))
    return rows


def covariance_filter(gamma_x, gamma_y, side, device=None):
    """Square root of the positive nearest-neighbour spectrum, one per plane."""
    import torch
    fy = torch.fft.fftfreq(side, device=device, dtype=torch.float64)
    fx = torch.fft.fftfreq(side, device=device, dtype=torch.float64)
    gx = torch.as_tensor(gamma_x, device=device, dtype=torch.float64)[:, None, None]
    gy = torch.as_tensor(gamma_y, device=device, dtype=torch.float64)[:, None, None]
    spectrum = 1+2*gx*torch.cos(2*math.pi*fx)[None, None, :]+2*gy*torch.cos(2*math.pi*fy)[None, :, None]
    if (spectrum <= 0).any():
        raise ValueError('Nonpositive photon covariance spectrum')
    return spectrum.sqrt().to(torch.float32)


def synthesise(clean, slope, read_dark, filters, divisor, generator):
    """Noisy normalized CFA from clean proxies, in sensor DN.

    clean: [B,4,H,W] normalized by `divisor`; slope: [B,4,1,1] DN per DN;
    read_dark: [B,4,H,W] dark-frame deviations in DN; filters: [B,4,H,W].
    """
    import torch
    signal = clean*divisor
    photons = signal.clamp_min(0)
    innovations = slope*torch.poisson(photons/slope, generator=generator)-photons
    innovations = torch.fft.ifft2(torch.fft.fft2(innovations)*filters).real
    observed = torch.round(signal+innovations)+read_dark
    observed = (observed+TARGET_BLACK).clamp(0, ADC_MAX)-TARGET_BLACK
    return observed/divisor


def sigma_map(noisy, slope, read_variance, divisor, window=5):
    """Same estimator everywhere: local mean per plane, then a*max(mean,0)+b."""
    import torch
    mean = torch.nn.functional.avg_pool2d(noisy, window, stride=1, padding=window//2, count_include_pad=False)
    a, b = slope/divisor, read_variance/divisor**2
    return (a*mean.clamp_min(0)+b).sqrt()


def prepare(output):
    natural = read(NATURAL/'result.json')
    roles = {}
    for entry in natural['entries']:
        roles.setdefault(entry['source'], dict(source=entry['source'], sha256=entry['source_sha256'], role=entry['role']))
    sources = sorted(roles.values(), key=lambda r: r['source'])
    if sorted(r['role'] for r in sources).count('training') != 2:
        raise ValueError('Expected two training and two development sources')
    darks = {}
    for path in DARK_GROUPS:
        for group in read(path)['groups']:
            if group['iso'] in TRAINING_ISO:
                darks[group['iso']] = [dict(path=s['path'], sha256=s['sha256'], role=s['role'])
                                       for s in group['source_identities']]
    if sorted(darks) != sorted(TRAINING_ISO):
        raise ValueError('Dark calibration/control sources missing for a training ISO')
    output.mkdir(exist_ok=False)
    tools = ['physical_natural_training.py', 'natural_cfa_preflight.py', 'dru_cfa.py', 'dru_checkpoint.py',
             'prepare_next_training.py', 'memory_budget.py', 'load_control.py', 'campaign_artifacts.py']
    plan = dict(schema_version=1, status='frozen_plan_a_development_campaign',
        created_at_utc=datetime.now(timezone.utc).isoformat(), campaign_seconds=2400, grace_seconds=45,
        seeds=[71, 73], data_seed=8101, steps=2048, batch=8, packed_side=256, parameters=8162688, width=32,
        precision='fp32', activation_checkpointing=True, learning_rate=.0001, objective='unweighted L1',
        gradient_clip_norm=1., controls_at=[0, 512, 1024, 2048], checkpoint_at=[1024],
        training_iso=list(TRAINING_ISO), iso_sampling='uniform over training ISO',
        sigma_estimator='sqrt(a*max(box5(noisy),0)+b) per plane, noisy input only',
        source_domain=dict(black=SOURCE_BLACK, white=SOURCE_WHITE, divisor=SOURCE_WHITE-SOURCE_BLACK),
        target_domain=dict(black=TARGET_BLACK, white=TARGET_WHITE, divisor=TARGET_WHITE-TARGET_BLACK, adc_max=ADC_MAX),
        tile_cfa_sites=TILE, training_tiles='non-overlapping 512-site grid over each training source',
        development_tiles='the 18 archived native v002 development patches',
        sources=sources, darks={str(k): v for k, v in darks.items()},
        noise_parameters=noise_parameters(read(BANK), read(COVARIANCE)),
        parents={str(p.relative_to(ROOT)): digest(p) for p in (NATURAL/'result.json', NATURAL/'targets.npy', BANK, COVARIANCE, *DARK_GROUPS)},
        tools={name: digest(ROOT/'tests/neural'/name) for name in tools},
        limits=['ISO100 single frames are clean proxies with their own residual noise, not truth',
                'Two training and two development sources from four days; development patches already inspected; no final test',
                'Target white 14351 measured at ISO16000 is assumed for 3200, 6400 and 51200; black 2048',
                'Shot slopes are descriptive fits; spectrum equivalence not demonstrated (15 of 20 inconclusive)',
                'ISO51200 real variance 2.6 to 5.0 % above the simulator',
                'Dark deviations keep read, row, column and fixed pattern of calibration frames; control frames for development',
                'No 12800/25600 (no lit slope), no 60D, no Bayer augmentation, no saturation mask in the loss',
                'Existing x - sigma*F adapter kept; adapter ablation and comparators (BM3D, classical IPR) not in this run'])
    write(output/'plan.json', plan)


def extract(output, guard):
    import numpy as np
    import rawpy
    plan = read(output/'plan.json')
    rng = np.random.default_rng(plan['data_seed'])
    divisor = plan['source_domain']['divisor']
    tiles, entries = [], []
    for row in plan['sources']:
        if row['role'] != 'training':
            continue
        guard.check()
        path = Path(row['source'])
        if digest(path) != row['sha256']:
            raise ValueError('Training source changed: '+str(path))
        with rawpy.imread(str(path)) as raw:
            if raw.raw_colors_visible[:2, :2].tolist() != [[0, 1], [3, 2]]:
                raise ValueError('RGGB R6 III phase required')
            mosaic = raw.raw_image_visible
            for y, x in grid_origins(*mosaic.shape):
                packed = source_normalized(mosaic[y:y+TILE, x:x+TILE], divisor)
                tiles.append(packed.astype('<f4'))
                entries.append(dict(source=row['source'], crop_xy=[x, y],
                    sha256=hashlib.sha256(tiles[-1].tobytes()).hexdigest()))
            del mosaic
    natural = read(NATURAL/'result.json')
    archived = np.load(NATURAL/'targets.npy', allow_pickle=False)
    check = []
    for index, entry in enumerate(natural['entries']):
        if entry['role'] != 'training':
            continue
        x, y = entry['crop_xy']
        with rawpy.imread(entry['source']) as raw:
            packed = source_normalized(raw.raw_image_visible[y:y+TILE, x:x+TILE], divisor)
        check.append(bool(np.array_equal(packed.astype('<f4'), archived[index])))
    if not all(check):
        raise ValueError('Grid extraction does not reproduce the archived native patches')
    development = archived[[i for i, e in enumerate(natural['entries']) if e['role'] != 'training']]
    np.save(output/'training.npy', np.stack(tiles), allow_pickle=False)
    np.save(output/'development.npy', development, allow_pickle=False)
    dark_entries = {}
    for iso, frames in plan['darks'].items():
        crops, pedestals, identities = [], [], []
        for frame in frames:
            guard.check()
            path = Path(frame['path'])
            if digest(path) != frame['sha256']:
                raise ValueError('Dark source changed: '+str(path))
            with rawpy.imread(str(path)) as raw:
                if raw.raw_colors_visible[:2, :2].tolist() != [[0, 1], [3, 2]]:
                    raise ValueError('RGGB R6 III phase required')
                mosaic = raw.raw_image_visible
                pedestal = plane_pedestals(mosaic)
                count = 6 if len(frames) > 20 else 32
                if frame['role'] == 'control':
                    count = 2 if len(frames) > 20 else 8
                height, width = mosaic.shape
                for _ in range(count):
                    y = int(rng.integers(16, height-TILE-16))//2*2
                    x = int(rng.integers(16, width-TILE-16))//2*2
                    crops.append(pack_rggb(mosaic[y:y+TILE, x:x+TILE]).astype(np.uint16))
                    pedestals.append(pedestal)
                    identities.append(dict(sha256=frame['sha256'], role=frame['role'], crop_xy=[x, y]))
                del mosaic
        np.save(output/f'dark-{iso}.npy', np.stack(crops), allow_pickle=False)
        np.save(output/f'pedestal-{iso}.npy', np.stack(pedestals), allow_pickle=False)
        dark_entries[iso] = identities
    write(output/'data.json', dict(status='extracted_plan_a_inputs', training_tiles=len(tiles),
        archived_patch_reproduction=check, training_entries=entries, development_tiles=len(development),
        darks=dark_entries, files={p.name: digest(p) for p in sorted(output.glob('*.npy'))},
        rawpy_version=rawpy.__version__, libraw_version=list(rawpy.libraw_version)))


class NoiseModel:
    """GPU tensors of the frozen per-ISO parameters and dark libraries."""

    def __init__(self, output, plan, device):
        import numpy as np
        import torch
        self.isos = plan['training_iso']
        self.divisor = float(plan['target_domain']['divisor'])
        rows = plan['noise_parameters']
        def table(key):
            return torch.tensor([[next(r[key] for r in rows if r['iso'] == iso and r['plane'] == p) for p in range(4)]
                                 for iso in self.isos], device=device, dtype=torch.float32)
        self.slope, self.read = table('shot_slope_dn'), table('read_variance_dn2')
        side = plan['packed_side']
        self.filters = torch.stack([covariance_filter([next(r['gamma_x'] for r in rows if r['iso'] == iso and r['plane'] == p) for p in range(4)],
                                                      [next(r['gamma_y'] for r in rows if r['iso'] == iso and r['plane'] == p) for p in range(4)],
                                                      side, device) for iso in self.isos])
        data = read(output/'data.json')
        self.darks, self.roles = [], []
        for iso in self.isos:
            crops = np.load(output/f'dark-{iso}.npy', allow_pickle=False)
            pedestals = np.load(output/f'pedestal-{iso}.npy', allow_pickle=False)
            deviation = torch.from_numpy(crops.astype(np.float32)-pedestals[:, :, None, None].astype(np.float32))
            self.darks.append(deviation.to(device))
            self.roles.append(torch.tensor([e['role'] == 'calibration' for e in data['darks'][str(iso)]], device=device))

    def dark(self, iso_index, role, choice):
        pool = self.darks[iso_index][self.roles[iso_index] == (role == 'calibration')]
        return pool[choice % len(pool)]

    def sample(self, clean, iso_ids, dark, generator):
        slope = self.slope[iso_ids][:, :, None, None]
        noisy = synthesise(clean, slope, dark, self.filters[iso_ids], self.divisor, generator)
        sigma = sigma_map(noisy, slope, self.read[iso_ids][:, :, None, None], self.divisor)
        return noisy, sigma


def train(output, seed, guard):
    import numpy as np
    import torch
    from dru_cfa import DruCfaCorrector
    from dru_checkpoint import fingerprint
    from load_control import YieldController
    plan, data = read(output/'plan.json'), read(output/'data.json')
    for name, expected in data['files'].items():
        if digest(output/name) != expected:
            raise ValueError('Plan A input changed: '+name)
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0] < 4*1024**3:
        raise RuntimeError('GPU unavailable or busy')
    torch.cuda.set_per_process_memory_fraction(.5)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    external = BUILD/'third-party-drunet-v001'
    for item in read(external/'manifest.json')['files']:
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
    training = torch.from_numpy(np.load(output/'training.npy', allow_pickle=False)).cuda()
    development = torch.from_numpy(np.load(output/'development.npy', allow_pickle=False)).cuda()
    noise = NoiseModel(output, plan, 'cuda')
    rng = torch.Generator(device='cuda').manual_seed(plan['data_seed'])
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    deadline = datetime.fromisoformat(read(output/'launch.json')['deadline_utc']).timestamp()
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
        if time.time() >= deadline or (output/'STOP').exists():
            raise InterruptedError('Deadline or STOP: save last complete update')
        if load.check():
            raise InterruptedError('Sustained host pressure: save last complete update')

    def checkpoint(name):
        temporary = folder/(name+'.tmp')
        try:
            torch.save(dict(schema_version=1, context=context, completed_steps=completed, batch=plan['batch'],
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
            for index, target in enumerate(development):
                for iso_index, iso in enumerate(plan['training_iso']):
                    check()
                    g = torch.Generator(device='cuda').manual_seed(9101000+index*8+iso_index)
                    clean = target.unsqueeze(0)
                    ids = torch.tensor([iso_index], device='cuda')
                    dark = noise.dark(iso_index, 'control', index).unsqueeze(0)
                    noisy, sigma = noise.sample(clean, ids, dark, g)
                    corrected = model.correct_observations(noisy, sigma, tags)
                    error = (corrected-clean)[..., 32:-32, 32:-32]
                    original = (noisy-clean)[..., 32:-32, 32:-32]
                    mse, before = float(error.square().mean()), float(original.square().mean())
                    rows.append(dict(target_index=index, iso=iso, corrected_mse=mse, observed_mse=before,
                        psnr_gain_db=10*math.log10(before/max(mse, 1e-30)),
                        mean_error_by_plane=error.mean(dim=(-2, -1))[0].tolist(),
                        mean_sigma_by_plane=sigma.mean(dim=(-2, -1))[0].tolist()))
        result['controls'].append(dict(step=step, rows=rows))
        model.train()
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
            iso_ids = torch.randint(len(plan['training_iso']), (plan['batch'],), device='cuda', generator=rng)
            choices = torch.randint(1 << 30, (plan['batch'],), device='cuda', generator=rng).tolist()
            dark = torch.stack([noise.dark(int(i), 'calibration', c) for i, c in zip(iso_ids.tolist(), choices)])
            target = training[ids]
            noisy, sigma = noise.sample(target, iso_ids, dark, rng)
            prediction = model.correct_observations(noisy, sigma, tags.expand(plan['batch'], -1))
            loss = torch.nn.functional.l1_loss(prediction, target)
            if not torch.isfinite(loss):
                raise ValueError('Nonfinite L1 loss')
            loss.backward()
            norm = torch.nn.utils.clip_grad_norm_(model.parameters(), plan['gradient_clip_norm'], error_if_nonfinite=True)
            optimizer.step()
            optimizer.zero_grad(set_to_none=True)
            torch.cuda.synchronize()
            result['update_seconds'].append(time.perf_counter()-started)
            result['losses'].append(dict(l1=float(loss.detach()), gradient_norm_before_clip=float(norm),
                                         clipped=bool(float(norm) > plan['gradient_clip_norm'])))
            completed = step
            result['completed_steps'] = step
            if step % 32 == 0:
                write(folder/'progress.json', result)
            if step in plan['controls_at']:
                control(step)
            if step in plan['checkpoint_at']:
                checkpoint(f'step{step}.pt')
        result['status'] = 'completed_plan_a_development_training'
    except Exception as error:
        result.update(status='stopped', error=type(error).__name__+': '+str(error))
        raise
    finally:
        optimizer.zero_grad(set_to_none=True)
        checkpoint('resume.pt')
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(), memory=guard.stats(),
            final_checkpoint_sha256=digest(folder/'resume.pt'),
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


def child(output, mode, seed, check):
    python = ('C:/Users/victo/AppData/Local/Python/pythoncore-3.14-64/python.exe' if mode == 'extract'
              else str(ROOT.parent/'venv-neural-cuda/Scripts/python.exe'))
    args = [python, str(Path(__file__).resolve()), '--output', str(output), '--mode', mode, '--seed', str(seed)]
    with (output/f'{mode}-{seed}.log').open('x', encoding='utf-8') as log:
        process = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
            creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        try:
            while process.poll() is None:
                check()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
        finally:
            if process.poll() is None:
                (output/'STOP').write_text('Complete update and save checkpoint.\n', encoding='utf-8')
                try:
                    process.wait(timeout=45)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                                   capture_output=True, timeout=20, check=False)
                    if process.poll() is None:
                        process.kill()
            process.wait(timeout=15)
    return dict(mode=mode, seed=seed, command=args, returncode=process.returncode)


def run(output):
    from memory_budget import snapshot
    plan = read(output/'plan.json')
    if not (output/'data.json').exists():
        raise ValueError('Run --mode extract first')
    result = dict(status='admission', stages=[], started_at_utc=datetime.now(timezone.utc).isoformat())
    admission_limit = datetime.now(timezone.utc)+timedelta(minutes=10)
    deadline = None
    def check():
        limit = admission_limit if deadline is None else deadline+timedelta(seconds=plan['grace_seconds'])
        if datetime.now(timezone.utc) >= limit:
            raise InterruptedError('Admission or campaign limit')
        for name, expected in plan['tools'].items():
            if digest(ROOT/'tests/neural'/name) != expected:
                raise ValueError('Frozen plan A code changed')
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
            time.sleep(5)
        deadline = datetime.now(timezone.utc)+timedelta(seconds=plan['campaign_seconds'])
        write(output/'launch.json', dict(launched_at_utc=datetime.now(timezone.utc).isoformat(),
                                        deadline_utc=deadline.isoformat()))
        for seed in plan['seeds']:
            row = child(output, 'train', seed, check)
            result['stages'].append(row)
            if row['returncode']:
                raise RuntimeError(f'Training worker failed for seed {seed}')
        result['status'] = 'completed_both_seeds_development_only'
    except Exception as error:
        result.update(status='stopped', error=str(error))
    finally:
        result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        result['plan'] = plan
        result['data'] = {k: v for k, v in read(output/'data.json').items() if k != 'training_entries'}
        result['measurements'] = [read(output/f'seed{s}/progress.json') for s in plan['seeds']
                                  if (output/f'seed{s}/progress.json').exists()]
        write(output/'progress.json', result)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-physical-natural-training-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
            json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('prepare', 'extract', 'run', 'train'), required=True)
    parser.add_argument('--seed', type=int, default=71)
    args = parser.parse_args()
    output = args.output.resolve()
    if args.mode == 'prepare':
        prepare(output)
    elif args.mode == 'run':
        run(output)
    elif args.mode == 'extract':
        worker(output, 'extract', 0)
    else:
        worker(output, 'train', args.seed)
