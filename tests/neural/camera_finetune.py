"""Fine-tune the beta-1 U-Net with a second camera (Canon EOS 60D), stopped on a gain plateau.

Two arms start from the shipped beta-1 checkpoint (final-beta1-training-v002, step 150 000)
with its Adam moments, at the same constant learning rate after a short warm-up:

- mixed: each batch holds half R6 Mark III tiles under the beta-1 R6 noise model and half EOS
  60D tiles under the 60D noise model (qualified law, real lens-cap darks, ADC clip at white);
- r6only (control): the same number of steps on R6 tiles only, to separate the effect of the
  60D data from the effect of more steps.

Controls every `control_every` steps, all deterministic:
- r6_synthetic: the 18 beta-1 development tiles at ISO 3200 to 51200 (forgetting);
- d60_synthetic: held-out 60D ISO 100 tiles (other days than training) under 60D noise;
- d60_real: real 60D bursts (fixed camera), each frame scored against the mean of the other
  frames of its burst, whose own noise (qualified law, over n - 1) is removed; the reference is
  matched row by row to the frame first (100 Hz flicker with a slit shutter moves the level by
  a few per cent along the rows between frames).
The mixed arm stops when, past `min_steps`, neither 60D metric (mean over the training ISO of
the median gain) improved by `plateau_db` over the last `plateau_window` controls; the control
arm then runs the same number of steps. The campaign writes its evidence JSON and a Markdown
table of the curves, then puts the machine to sleep (`--sleep`).

Modes: prepare (freeze plan.json), extract (60D tiles, darks, bursts), train (one arm), run
(all). Needs the CUDA environment with torch and rawpy; development only, not a unit test.
"""
import argparse
import ctypes
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepare_next_training import digest, read, write  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
PARENT = BUILD/'final-beta1-training-v002'
QUALIFICATION = ROOT.parent/'patch_noise'/'docs/validation/raw'
PYTHON = ROOT.parent/'venv-neural-cuda/Scripts/python.exe'
DIVISOR = 12303.0          # network input unit of production (packaging/neural-model.json)
D60_ISO = (800, 1600, 3200, 6400)
TILE = 512                 # sensor sites per side; 256 per plane
CROP = 512                 # per plane, real bursts
TOOLS = ('camera_finetune.py', 'physical_natural_training.py', 'denoise_estimated_against_qualified.py',
         'input_modes.py', 'physical_training_objective.py', 'load_control.py', 'memory_budget.py',
         'prepare_next_training.py')


# ---------------------------------------------------------------- 60D data, in network units

def raw_planes(path):
    """Canonical R/G1/G2/B planes (DN minus black, over DIVISOR) and the white above black."""
    from denoise_estimated_against_qualified import blacks, canonical_planes, read_raw, CANONICAL_FROM_RASTER
    full, top, left, white, rows, width = read_raw(path)
    black = blacks(full, top, left, rows)
    planes = canonical_planes(full, top, left, rows, width, black)
    clip = [white-black[r] for r in CANONICAL_FROM_RASTER]
    return planes, clip


def law_dn(qualified, iso):
    """Canonical (shot slope DN/DN, read variance DN^2) per plane from the qualification."""
    from denoise_estimated_against_qualified import canonical_law, law_at
    law = law_at(qualified, iso)
    if law is None:
        raise ValueError(f'No qualified 60D law at ISO {iso}')
    return canonical_law(law)


def tile_origins(planes, clip, count, rng):
    """Up to `count` non-overlapping plane tiles with at most 1 % of sites at the white."""
    import numpy as np
    side = TILE//2
    _, h, w = planes.shape
    eligible = []
    for y in range(16, h-side-16, side):
        for x in range(16, w-side-16, side):
            tile = planes[:, y:y+side, x:x+side]*DIVISOR
            saturated = np.mean([(tile[p] >= clip[p]*.98).mean() for p in range(4)])
            if saturated <= .01:
                eligible.append((y, x))
    picked = rng.permutation(len(eligible))[:count]
    return [eligible[i] for i in sorted(picked.tolist())]


def row_matched(reference, frame, window=9):
    """Reference scaled row by row (smoothed row means) to the frame's level, per plane."""
    import numpy as np
    kernel = np.ones(window)/window
    out = reference.copy()
    for p in range(reference.shape[0]):
        r = np.convolve(reference[p].mean(axis=1), kernel, mode='same')
        f = np.convolve(frame[p].mean(axis=1), kernel, mode='same')
        scale = np.where(r > 20/DIVISOR, f/np.maximum(r, 1e-12), 1.)
        out[p] *= scale[:, None]
    return out


def bursts(inventory):
    groups, current = [], None
    for frame in inventory:
        key = (frame['iso'], frame['exposure_s'])
        if current is not None and current['key'] == key:
            current['frames'].append(frame)
        else:
            current = dict(key=key, frames=[frame])
            groups.append(current)
    return [g for g in groups if len(g['frames']) >= 3]


# ---------------------------------------------------------------- plan and extraction

def prepare(output, sources, qualification, darks_folder, inventory, args):
    qual = read(qualification)
    rows = read(sources)
    training = [r for r in rows if r['role'] == 'training']
    development = [r for r in rows if r['role'] == 'development']
    if {r['day'] for r in training} & {r['day'] for r in development}:
        raise ValueError('A development day is also a training day')
    dark_rows = [r for r in qual['inventory'] if r['role'] == 'dark' and r['iso'] in D60_ISO]
    darks = {}
    for iso in D60_ISO:
        frames = sorted((r for r in dark_rows if r['iso'] == iso), key=lambda r: r['file'])
        if len(frames) < 4:
            raise ValueError(f'Too few 60D darks at ISO {iso}')
        darks[str(iso)] = [dict(path=str(Path(darks_folder)/r['file']), sha256=r['sha256'],
                                role='control' if i >= len(frames)-2 else 'calibration') for i, r in enumerate(frames)]
    parent_plan = read(PARENT/'plan.json')
    parent_progress = read(PARENT/'seed71/progress.json')
    if digest(PARENT/'seed71/resume.pt') != parent_progress['final_checkpoint_sha256']:
        raise ValueError('Beta-1 final checkpoint changed')
    output.mkdir(parents=True, exist_ok=False)
    plan = dict(schema_version=1, status='frozen_camera_finetune', created_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(campaign=str(PARENT), plan_sha256=digest(PARENT/'plan.json'), checkpoint=str(PARENT/'seed71/resume.pt'),
                    checkpoint_sha256=parent_progress['final_checkpoint_sha256'], completed_steps=150000,
                    model='ipr-neural-denoise-final-beta1-step150000'),
        input_mode=parent_plan['input_mode'], objective=parent_plan['objective'], parameters=parent_plan['parameters'],
        color_augmentation=parent_plan['color_augmentation'], gradient_clip_norm=parent_plan['gradient_clip_norm'],
        activation_checkpointing=True, batch=8, d60_share_of_mixed_batch=.5, packed_side=TILE//2,
        learning_rate=args.learning_rate, warmup_steps=500, schedule='linear warm-up then constant',
        optimizer='Adam, moments restored from the beta-1 checkpoint', seed=71, data_seed=9301,
        r6=dict(training_iso=parent_plan['training_iso'], noise='beta-1 R6 III model (NoiseModel of the parent campaign)'),
        d60=dict(training_iso=list(D60_ISO), divisor=DIVISOR, qualification=str(qualification),
                 qualification_sha256=digest(qualification), photon_covariance='none (white shot noise): not measured on the 60D',
                 darks=darks, clip='white level of each dark frame minus its black'),
        sources=rows, tiles_per_source=args.tiles_per_source, development_tiles_per_source=3,
        bursts=dict(inventory=str(inventory), inventory_sha256=digest(inventory), crops='centre and lower right, 512 per plane',
                    reference='mean of the other frames, row-matched to the frame, own noise removed by the qualified law'),
        control_every=args.control_every, min_steps=args.min_steps, max_steps=args.max_steps,
        plateau=dict(window_controls=args.plateau_window, gain_db=args.plateau_db,
                     metrics=['d60_synthetic', 'd60_real'],
                     rule='stop when both metrics gained less than gain_db between the best of the earlier controls and the best of the last window'),
        forgetting_stop_db=.5, deadline_local=args.deadline,
        tools={name: digest(Path(__file__).resolve().parent/name) for name in TOOLS},
        limits=['ISO 100 60D frames are clean proxies with their own residual noise, not truth',
                '60D photon noise simulated white (no neighbour covariance measured), Poisson at the qualified slope',
                'Real bursts are the bracket series of 13 November under 100 Hz light; row matching removes most of the flicker, not all',
                'The bursts are the only real 60D noise: they are never used for training'])
    write(output/'plan.json', plan)


def extract(output):
    import numpy as np
    plan = read(output/'plan.json')
    rng = np.random.default_rng(plan['data_seed'])
    side = TILE//2
    sets = {}
    for role, count in (('training', plan['tiles_per_source']), ('development', plan['development_tiles_per_source'])):
        rows = [r for r in plan['sources'] if r['role'] == role]
        tiles, entries = [], []
        for row in rows:
            if digest(Path(row['path'])) != row['sha256']:
                raise ValueError('60D source changed: '+row['path'])
            planes, clip = raw_planes(row['path'])
            for y, x in tile_origins(planes, clip, count, rng):
                tiles.append(planes[:, y:y+side, x:x+side].astype('<f4'))
                entries.append(dict(path=row['path'], plane_xy=[x, y]))
            del planes
        np.save(output/f'd60-{role}.npy', np.stack(tiles), allow_pickle=False)
        sets[role] = entries
    darks = {}
    for iso, frames in plan['d60']['darks'].items():
        crops, identities, clips = [], [], []
        for frame in frames:
            if digest(Path(frame['path'])) != frame['sha256']:
                raise ValueError('60D dark changed: '+frame['path'])
            planes, clip = raw_planes(frame['path'])
            deviation = (planes-planes.mean(axis=(1, 2), keepdims=True))*DIVISOR
            _, h, w = planes.shape
            for _ in range(16 if frame['role'] == 'calibration' else 4):
                y = int(rng.integers(8, h-side-8))
                x = int(rng.integers(8, w-side-8))
                crops.append(deviation[:, y:y+side, x:x+side].astype('<f4'))
                identities.append(dict(sha256=frame['sha256'], role=frame['role'], plane_xy=[x, y]))
            clips.append(clip)
            del planes, deviation
        np.save(output/f'd60-dark-{iso}.npy', np.stack(crops), allow_pickle=False)
        darks[iso] = dict(crops=identities, clip_dn=np.min(np.array(clips), axis=0).tolist())
    # Real bursts: inputs and row-matched references, per frame and crop.
    inventory = read(Path(plan['bursts']['inventory']))
    inputs, references, rows = [], [], []
    for burst in bursts(inventory):
        iso, exposure = burst['key']
        stack = np.stack([raw_planes(f['path'])[0] for f in burst['frames']])
        n = len(stack)
        _, _, h, w = stack.shape
        for i, frame in enumerate(burst['frames']):
            reference = (stack.sum(axis=0)-stack[i])/(n-1)
            for y, x in ((h//2-CROP//2, w//2-CROP//2), (h-h//8-CROP, w-w//8-CROP)):
                inp = stack[i][:, y:y+CROP, x:x+CROP]
                inputs.append(inp.astype('<f4'))
                references.append(row_matched(reference[:, y:y+CROP, x:x+CROP], inp).astype('<f4'))
                rows.append(dict(iso=iso, exposure_s=exposure, frames=n, file=Path(frame['path']).name, plane_xy=[x, y]))
        del stack
    np.save(output/'burst-input.npy', np.stack(inputs), allow_pickle=False)
    np.save(output/'burst-reference.npy', np.stack(references), allow_pickle=False)
    write(output/'data.json', dict(status='extracted_camera_finetune_inputs', d60=sets, d60_darks=darks, bursts=rows,
        files={p.name: digest(p) for p in sorted(output.glob('*.npy'))}))


# ---------------------------------------------------------------- training

class D60Noise:
    """60D noise in network units: Poisson at the qualified slope, real dark deviations, clip."""

    def __init__(self, output, plan, data, device):
        import numpy as np
        import torch
        qual = read(Path(plan['d60']['qualification']))
        self.isos = plan['d60']['training_iso']
        laws = [law_dn(qual, iso) for iso in self.isos]
        self.slope = torch.tensor([[a for a, _ in law] for law in laws], device=device, dtype=torch.float32)
        self.read = torch.tensor([[b for _, b in law] for law in laws], device=device, dtype=torch.float32)
        self.clip = torch.tensor([data['d60_darks'][str(iso)]['clip_dn'] for iso in self.isos], device=device,
                                 dtype=torch.float32)
        self.crops = [np.load(output/f'd60-dark-{iso}.npy', mmap_mode='r') for iso in self.isos]
        self.pools = [{role: [i for i, c in enumerate(data['d60_darks'][str(iso)]['crops']) if c['role'] == role]
                       for role in ('calibration', 'control')} for iso in self.isos]
        self.device = device

    def dark(self, iso_index, role, choice):
        import numpy as np
        import torch
        pool = self.pools[iso_index][role]
        return torch.from_numpy(np.array(self.crops[iso_index][pool[choice % len(pool)]])).to(self.device)

    def law(self, iso_ids):
        return (self.slope[iso_ids]/DIVISOR)[:, :, None, None], (self.read[iso_ids]/DIVISOR**2)[:, :, None, None]

    def sample(self, clean, iso_ids, dark, generator):
        import torch
        from physical_natural_training import sigma_map
        slope = self.slope[iso_ids][:, :, None, None]
        signal = clean*DIVISOR
        photons = signal.clamp_min(0)
        observed = slope*torch.poisson(photons/slope, generator=generator)
        observed = torch.round(observed)+dark
        observed = torch.minimum(observed.clamp_min(-2048.), self.clip[iso_ids][:, :, None, None])
        noisy = observed/DIVISOR
        return noisy, sigma_map(noisy, slope, self.read[iso_ids][:, :, None, None], DIVISOR)


def build_model(plan):
    import torch
    from input_modes import ModeCorrector, in_channels
    external = BUILD/'third-party-drunet-v001'
    sys.path.insert(0, str(external))
    from models.network_unet import UNetRes
    core = UNetRes(in_nc=in_channels(plan['input_mode']), out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                   downsample_mode='strideconv', upsample_mode='convtranspose')
    model = ModeCorrector(core, plan['input_mode'], checkpoint_stages=plan['activation_checkpointing']).cuda()
    if sum(p.numel() for p in model.parameters()) != plan['parameters']:
        raise ValueError('Unexpected parameter count')
    return model


def median_by_iso(rows, isos):
    import numpy as np
    return {str(iso): float(np.median([r['gain_db'] for r in rows if r['iso'] == iso])) for iso in isos
            if any(r['iso'] == iso for r in rows)}


def plateau_reached(controls, plan):
    """True when no 60D metric improved by gain_db over the last window of controls."""
    window = plan['plateau']['window_controls']
    if len(controls) <= window or controls[-1]['step'] < plan['min_steps']:
        return False
    for metric in plan['plateau']['metrics']:
        values = [c['summary'][metric] for c in controls]
        if max(values[-window:])-max(values[:-window]) >= plan['plateau']['gain_db']:
            return False
    return True


def train(output, arm, steps_limit):
    import numpy as np
    import torch
    from input_modes import mode_loss
    from load_control import YieldController
    from memory_budget import MemoryGuard
    from physical_natural_training import CANONICAL_TAGS, NoiseModel, color_augment
    plan, data = read(output/'plan.json'), read(output/'data.json')
    for name, expected in data['files'].items():
        if digest(output/name) != expected:
            raise ValueError('Input changed: '+name)
    parent_plan = read(PARENT/'plan.json')
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0] < 4*1024**3:
        raise RuntimeError('GPU unavailable or busy')
    torch.cuda.set_per_process_memory_fraction(.5)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    torch.manual_seed(plan['seed'])
    model = build_model(plan)
    state = torch.load(plan['parent']['checkpoint'], map_location='cpu', weights_only=True)
    if digest(Path(plan['parent']['checkpoint'])) != plan['parent']['checkpoint_sha256']:
        raise ValueError('Parent checkpoint changed')
    model.load_state_dict(state['model_state'], strict=True)
    optimizer = torch.optim.Adam(model.parameters(), lr=plan['learning_rate'])
    optimizer.load_state_dict(state['optimizer_state'])
    del state
    r6_training = np.load(PARENT/'training.npy', mmap_mode='r')[:read(PARENT/'data.json')['training_tiles']]
    r6_development = torch.from_numpy(np.load(PARENT/'development.npy')).cuda()
    r6_noise = NoiseModel(PARENT, parent_plan, 'cuda')
    d60_training = np.load(output/'d60-training.npy', mmap_mode='r')
    d60_development = torch.from_numpy(np.load(output/'d60-development.npy')).cuda()
    d60_noise = D60Noise(output, plan, data, 'cuda')
    burst_input = np.load(output/'burst-input.npy', mmap_mode='r')
    burst_reference = np.load(output/'burst-reference.npy', mmap_mode='r')
    qual = read(Path(plan['d60']['qualification']))
    burst_laws = {iso: law_dn(qual, iso) for iso in {r['iso'] for r in data['bursts']}}
    rng = torch.Generator(device='cuda').manual_seed(plan['data_seed']+(0 if arm == 'mixed' else 1))
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    folder = output/f'arm-{arm}'
    folder.mkdir(exist_ok=False)
    deadline = datetime.fromisoformat(plan['deadline_local']).timestamp()
    load = YieldController(dict(yield_memory_fraction=.85, yield_external_cpu_fraction=.80, pressure_seconds=15))
    result = dict(status='training', arm=arm, started_at_utc=datetime.now(timezone.utc).isoformat(),
                  plan_sha256=digest(output/'plan.json'), steps_limit=steps_limit, controls=[], losses=[],
                  update_seconds=[], paused_seconds=0., completed_steps=0)

    def check(guard):
        guard.check()
        if time.time() >= deadline or (output/'STOP').exists():
            raise InterruptedError('Deadline or STOP')
        if load.check():
            # Another program needs the machine: wait instead of stopping the night's campaign.
            started = time.time()
            while load.check() or load.pressure_since is not None:
                if time.time() >= deadline or (output/'STOP').exists():
                    raise InterruptedError('Deadline or STOP during a pause')
                time.sleep(10)
            result['paused_seconds'] += time.time()-started

    def synthetic(target_set, noise, isos, seed_base, guard):
        rows = []
        for index, target in enumerate(target_set):
            for iso_index, iso in enumerate(isos):
                check(guard)
                g = torch.Generator(device='cuda').manual_seed(seed_base+index*8+iso_index)
                clean = target.unsqueeze(0)
                ids = torch.tensor([iso_index], device='cuda')
                noisy, sigma = noise.sample(clean, ids, noise.dark(iso_index, 'control', index).unsqueeze(0), g)
                a, b = noise.law(ids)
                corrected = model.correct(noisy, sigma, tags, a, b)['corrected']
                mse = float((corrected-clean)[..., 32:-32, 32:-32].square().mean())
                before = float((noisy-clean)[..., 32:-32, 32:-32].square().mean())
                rows.append(dict(index=index, iso=iso, gain_db=10*math.log10(before/max(mse, 1e-30)),
                                 mean_error_by_plane=(corrected-clean)[..., 32:-32, 32:-32].mean(dim=(-2, -1))[0].tolist()))
        return rows

    def real(guard):
        from physical_natural_training import sigma_map
        rows = []
        for index, meta in enumerate(data['bursts']):
            check(guard)
            law = burst_laws[meta['iso']]
            slope = torch.tensor([a for a, _ in law], device='cuda', dtype=torch.float32)[None, :, None, None]
            readv = torch.tensor([b for _, b in law], device='cuda', dtype=torch.float32)[None, :, None, None]
            x = torch.from_numpy(np.array(burst_input[index])).cuda()[None]
            ref = torch.from_numpy(np.array(burst_reference[index])).cuda()[None]
            sigma = sigma_map(x, slope, readv, DIVISOR)
            out = model.correct(x, sigma, tags, slope/DIVISOR, readv/DIVISOR**2)['corrected']
            ref_var = float(((slope*(ref*DIVISOR).clamp_min(0)+readv)/DIVISOR**2/(meta['frames']-1)).mean())
            before = float((x-ref).square().mean())-ref_var
            after = float((out-ref).square().mean())-ref_var
            rows.append(dict(index=index, iso=meta['iso'], gain_db=10*math.log10(max(before, 1e-20)/max(after, 1e-20))))
        return rows

    def control(step, guard):
        model.eval()
        with torch.inference_mode():
            r6 = synthetic(r6_development, r6_noise, parent_plan['training_iso'], 9101000, guard)
            d60 = synthetic(d60_development, d60_noise, plan['d60']['training_iso'], 9301000, guard)
            burst = real(guard)
        model.train()
        torch.cuda.empty_cache()
        trim_working_set()
        by = dict(r6_synthetic=median_by_iso(r6, parent_plan['training_iso']),
                  d60_synthetic=median_by_iso(d60, plan['d60']['training_iso']),
                  d60_real=median_by_iso(burst, sorted({r['iso'] for r in burst})))
        summary = dict(r6_synthetic=float(np.mean(list(by['r6_synthetic'].values()))),
                       d60_synthetic=float(np.mean(list(by['d60_synthetic'].values()))),
                       d60_real=float(np.mean([by['d60_real'][str(i)] for i in plan['d60']['training_iso']])))
        result['controls'].append(dict(step=step, at_utc=datetime.now(timezone.utc).isoformat(), summary=summary,
                                       by_iso=by, rows=dict(r6_synthetic=r6, d60_synthetic=d60, d60_real=burst)))
        torch.save(model.state_dict(), folder/f'model-step{step}.pt')
        write(folder/'progress.json', result)
        print(step, json.dumps(summary), flush=True)

    def stop_reason(step):
        controls = result['controls']
        if arm == 'mixed' and plateau_reached(controls, plan):
            return 'plateau'
        base = controls[0]['summary']['r6_synthetic']
        if arm == 'mixed' and len(controls) >= 2 and all(base-c['summary']['r6_synthetic'] > plan['forgetting_stop_db']
                                                          for c in controls[-2:]):
            return 'forgetting'
        return None

    half = int(plan['batch']*plan['d60_share_of_mixed_batch']) if arm == 'mixed' else 0
    with MemoryGuard(hard_bytes=6*1024**3, soft_bytes=5*1024**3, system_fraction=.92) as guard:
        try:
            control(0, guard)
            for step in range(1, steps_limit+1):
                check(guard)
                started = time.perf_counter()
                lr = plan['learning_rate']*min(1., step/plan['warmup_steps'])
                for group in optimizer.param_groups:
                    group['lr'] = lr
                parts = []
                for noise, tiles, count in ((r6_noise, r6_training, plan['batch']-half), (d60_noise, d60_training, half)):
                    if count == 0:
                        continue
                    ids = torch.randint(len(tiles), (count,), device='cuda', generator=rng).tolist()
                    iso_ids = torch.randint(len(noise.isos), (count,), device='cuda', generator=rng)
                    choices = torch.randint(1 << 30, (count,), device='cuda', generator=rng).tolist()
                    dark = torch.stack([noise.dark(int(i), 'calibration', c) for i, c in zip(iso_ids.tolist(), choices)])
                    target = torch.from_numpy(np.stack([tiles[i] for i in ids])).cuda()
                    target = color_augment(target, rng, plan['color_augmentation']['probability'],
                                           plan['color_augmentation']['red_blue_gain_range'],
                                           plan['color_augmentation'].get('green_gain_range', (1., 1.)))
                    if noise is d60_noise:
                        # The augmentation clips at 1 (R6 white); the 60D saturates lower, at its own white.
                        target = torch.minimum(target, d60_noise.clip[iso_ids][:, :, None, None]/DIVISOR)
                    noisy, sigma = noise.sample(target, iso_ids, dark, rng)
                    a, b = noise.law(iso_ids)
                    parts.append((target, noisy, sigma, a, b))
                target, noisy, sigma, a, b = (torch.cat(t) for t in zip(*parts))
                planes = model.correct(noisy, sigma, tags.expand(plan['batch'], -1), a, b)
                loss = mode_loss(plan['input_mode'], plan['objective'], planes, target, sigma, a, b, 1/DIVISOR, 1.)
                if not torch.isfinite(loss):
                    raise ValueError('Nonfinite loss')
                optimizer.zero_grad(set_to_none=True)
                loss.backward()
                norm = torch.nn.utils.clip_grad_norm_(model.parameters(), plan['gradient_clip_norm'], error_if_nonfinite=True)
                optimizer.step()
                torch.cuda.synchronize()
                result['update_seconds'].append(time.perf_counter()-started)
                if step % 200 == 0:
                    trim_working_set()
                if step % 100 == 0:
                    result['losses'].append(dict(step=step, objective_loss=float(loss.detach()), gradient_norm=float(norm)))
                result['completed_steps'] = step
                if step % plan['control_every'] == 0 or step == steps_limit:
                    control(step, guard)
                    reason = stop_reason(step)
                    if reason:
                        result['status'] = reason
                        break
            else:
                result['status'] = 'steps_limit'
        except InterruptedError as error:
            result['status'] = 'interrupted: '+str(error)
        except Exception as error:
            result['status'] = 'failed: '+type(error).__name__+': '+str(error)
            raise
        finally:
            torch.save(dict(model_state=model.state_dict(), optimizer_state=optimizer.state_dict(),
                            completed_steps=result['completed_steps']), folder/'resume.pt')
            result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(), memory=guard.stats(),
                          final_checkpoint_sha256=digest(folder/'resume.pt'),
                          peak_cuda_reserved_bytes=torch.cuda.max_memory_reserved())
            write(folder/'progress.json', result)


# ---------------------------------------------------------------- campaign

def trim_working_set():
    """Hand the pages of the memory-mapped tiles back to the standby cache (Windows).

    Pages read through np.load(mmap_mode='r') stay in the working set and count as used
    memory; without this, the campaign's own reads would trigger its host-pressure pause.
    """
    if sys.platform == 'win32':
        ctypes.windll.psapi.EmptyWorkingSet(ctypes.windll.kernel32.GetCurrentProcess())


def keep_awake(on):
    """Keep Windows from sleeping while the campaign runs (ES_CONTINUOUS | ES_SYSTEM_REQUIRED)."""
    if sys.platform == 'win32':
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000000 | (0x00000001 if on else 0))


def child(output, mode, arm=None, steps=None):
    args = [str(PYTHON), str(Path(__file__).resolve()), '--output', str(output), '--mode', mode]
    if arm:
        args += ['--arm', arm, '--steps', str(steps)]
    with (output/f'{mode}-{arm or "data"}.log').open('a', encoding='utf-8') as log:
        process = subprocess.Popen(args, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        return process.wait()


def markdown(result):
    lines = ['| Arm | Step | 60D synthetic (dB) | 60D real (dB) | R6 III synthetic (dB) | 60D real by ISO |',
             '| --- | ---: | ---: | ---: | ---: | --- |']
    for arm in result['arms']:
        for c in arm['controls']:
            s = c['summary']
            iso = ' '.join(f"{k}:{v:+.2f}" for k, v in c['by_iso']['d60_real'].items())
            lines.append(f"| {arm['arm']} | {c['step']} | {s['d60_synthetic']:+.3f} | {s['d60_real']:+.3f} | "
                         f"{s['r6_synthetic']:+.3f} | {iso} |")
    return '\n'.join(lines)+'\n'


def run(output, sleep):
    plan = read(output/'plan.json')
    keep_awake(True)
    result = dict(status='running', started_at_utc=datetime.now(timezone.utc).isoformat(), arms=[])
    try:
        for name, expected in plan['tools'].items():
            if digest(Path(__file__).resolve().parent/name) != expected:
                raise ValueError('Frozen tool changed: '+name)
        if not (output/'data.json').exists() and child(output, 'extract'):
            raise RuntimeError('Extraction failed')
        code = child(output, 'train', 'mixed', plan['max_steps'])
        mixed = read(output/'arm-mixed/progress.json')
        result['arms'].append(mixed)
        if code or mixed['status'].startswith(('failed', 'interrupted')):
            raise RuntimeError('Mixed arm ended with '+mixed['status'])
        code = child(output, 'train', 'r6only', mixed['completed_steps'])
        control = read(output/'arm-r6only/progress.json')
        result['arms'].append(control)
        result['status'] = 'completed' if not code else 'control arm ended with '+control['status']
    except Exception as error:
        result['status'] = 'stopped: '+str(error)
    finally:
        result['finished_at_utc'] = datetime.now(timezone.utc).isoformat()
        result['plan'] = {k: v for k, v in plan.items() if k != 'sources'}
        result['sources'] = plan['sources']
        for arm in result['arms']:
            arm.pop('update_seconds', None)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        evidence = ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json'
        evidence.write_text(json.dumps(result, allow_nan=False, separators=(',', ':')), encoding='utf-8')
        (output/'curves.md').write_text(f"Campaign {output.name}: {result['status']}\n\n"+markdown(result), encoding='utf-8')
        keep_awake(False)
        if sleep:
            subprocess.run(['powershell', '-NoProfile', '-Command',
                            'Add-Type -AssemblyName System.Windows.Forms; '
                            '[System.Windows.Forms.Application]::SetSuspendState("Suspend", $false, $false)'],
                           check=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('prepare', 'extract', 'train', 'run'), required=True)
    parser.add_argument('--sources', type=Path)
    parser.add_argument('--qualification', type=Path)
    parser.add_argument('--darks', type=Path)
    parser.add_argument('--inventory', type=Path)
    parser.add_argument('--arm', choices=('mixed', 'r6only'))
    parser.add_argument('--steps', type=int)
    parser.add_argument('--learning-rate', type=float, default=2e-5)
    parser.add_argument('--tiles-per-source', type=int, default=12)
    parser.add_argument('--control-every', type=int, default=2500)
    parser.add_argument('--min-steps', type=int, default=15000)
    parser.add_argument('--max-steps', type=int, default=100000)
    parser.add_argument('--plateau-window', type=int, default=4)
    parser.add_argument('--plateau-db', type=float, default=.02)
    parser.add_argument('--deadline', default='2026-11-19T07:00:00+01:00')
    parser.add_argument('--sleep', action='store_true')
    args = parser.parse_args()
    output = args.output.resolve()
    if args.mode == 'prepare':
        prepare(output, args.sources, args.qualification, args.darks, args.inventory, args)
    elif args.mode == 'extract':
        extract(output)
    elif args.mode == 'train':
        train(output, args.arm, args.steps)
    else:
        run(output, args.sleep)
