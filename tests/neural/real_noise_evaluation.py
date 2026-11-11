"""Real-noise evaluation of the denoisers on paired real scenes.

Scene sets: the interior scenes of 1 November (references cached by
real_noise_bench.py) and the multicoloured sheet of 10 November (reference
cached by color_sheet_audit.py, four exposures per ISO).
Reference: the cached ISO100 burst mean of each scene.
For a few frames of each high-ISO burst, a central packed crop is aligned on the
reference by an integer packed-cell shift (Bayer phase kept, no resampling),
then denoised by the network (CUDA environment) and by VST + BM3D (external
environment). Each frame gets its own per-plane gain/offset onto the reference,
fitted on 8x8 block means of the noisy frame only, so the same mapping applies
to every method and light drift is absorbed. Scores are MSE against the
reference, gain over the noisy frame, with the ISO100 reference noise floor
reported beside them.

A second reference avoids the scene mismatch between the ISO100 burst and the
high-ISO bursts (camera shift, light change): for each evaluated frame, the
mean of the other frames of its own burst, on the same crop. Its noise is
known from the measured law, (a*y + b)/(N - 1), and is subtracted from every
MSE; fixed-pattern noise shared by the burst stays in this reference, so a
denoiser that removes it is penalized there.

Modes, one environment each:
  prepare  system Python with rawpy: read frames, align, write crops.npz
  burst    system Python with rawpy: sums of every burst on the same crops, burst.npz
  network  CUDA venv: network outputs per checkpoint, network.npz
  bm3d     external BM3D environment: bm3d.npz
  score    any numpy environment: result.json and the proof JSON
Read only on the card; development diagnosis, not a qualification.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path

from prepare_next_training import digest, read, write
from real_noise_bench import CARD


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
BENCH = BUILD/'real-noise-bench-v001'
COLOR_AUDIT = BUILD/'color-sheet-audit-v001'
BANK = ROOT/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
DIVISOR = 12303.
NOMINAL_BLACK = 2048.
DARK_PEDESTALS = BUILD/'physical-natural-training-v005'
CROP = (1024, 1536)
BORDER = 32
BLOCK = 8
TRAINED_ISO = (3200, 6400, 16000, 51200)


def scene_sets():
    """Per set: frames evaluated per burst and bursts (scene, ISO, first, last, tag); the leaves scene has no stable reference."""
    from color_sheet_audit import BURSTS as SHEET
    interior = (('interior_night', 1600, 310, 319, ''), ('interior_night', 3200, 328, 339, ''),
                ('interior_night', 6400, 340, 351, ''), ('interior_night', 12800, 352, 366, ''),
                ('interior_day', 3200, 152, 180, ''), ('interior_day', 12800, 181, 194, ''), ('interior_day', 51200, 195, 209, ''))
    sheet = tuple(('color_sheet', iso, first, last, f'1-{round(1/shutter)}') for iso, shutter, first, last in SHEET if iso > 100)
    return {'interior': dict(frames_per_burst=3, bursts=interior), 'color_sheet': dict(frames_per_burst=2, bursts=sheet)}


def load_reference(scene):
    """ISO100 mean of the scene, checked against the audit that cached it, with its frame count and temporal sigma."""
    import numpy as np
    if scene == 'color_sheet':
        audit = read(COLOR_AUDIT/'result.json')
        row = audit['rows'][0]
        path, expected = COLOR_AUDIT/'iso100-brightest-mean.npy', row['reference_mean_sha256']
        frames, sigma, audit_path = row['frames'], row['temporal_sigma_by_plane'], COLOR_AUDIT/'result.json'
    else:
        bench = read(BENCH/'result.json')
        burst = bench['scenes'][scene]['bursts']['100']
        path, expected = BENCH/f'{scene}-iso100-mean.npy', burst['reference_mean_sha256']
        frames, sigma, audit_path = burst['frames'], burst['temporal_sigma_by_plane'], BENCH/'result.json'
    if digest(path) != expected:
        raise ValueError('Cached reference mean changed: '+scene)
    return np.load(path, allow_pickle=False), frames, sigma, audit_path


def audited_shift(scene, iso, first):
    if scene == 'color_sheet':
        rows = read(COLOR_AUDIT/'result.json')['rows']
        return next(r.get('shift_packed_cells') for r in rows if r['first'] == f'6V1A{first:04d}.CR3')
    return read(BENCH/'result.json')['scenes'][scene]['bursts'][str(iso)].get('shift_packed_cells')


def noise_law(bank, iso):
    """Per-plane (shot slope DN/DN, read variance DN^2) and its provenance; slope interpolated in log-log when missing."""
    fits = {(r['iso'], r['plane']): r for r in bank['fits']}
    rows, provenance = [], 'measured'
    for plane in range(4):
        fit = fits[(iso, plane)]
        slope = fit['fit'].get('shot_slope_dn')
        if slope is None:
            measured = sorted(i for (i, p), r in fits.items() if p == plane and r['fit'].get('shot_slope_dn'))
            low = max(i for i in measured if i < iso)
            high = min(i for i in measured if i > iso)
            s0, s1 = fits[(low, plane)]['fit']['shot_slope_dn'], fits[(high, plane)]['fit']['shot_slope_dn']
            t = math.log(iso/low)/math.log(high/low)
            slope = math.exp((1-t)*math.log(s0)+t*math.log(s1))
            provenance = f'shot slope interpolated log-log between ISO {low} and {high}; read variance measured'
        rows.append((float(slope), float(fit['reference_dark_variance_dn2'])))
    return rows, provenance


def dark_black(iso):
    """Visible black per plane: median pedestal of the training dark frames at this ISO, else the nominal 2048.

    rawpy's black_level_per_channel is not usable on these CR3, and the masked
    columns of lit frames read 10 to 40 DN above the dark frames' visible level.
    """
    import numpy as np
    path = DARK_PEDESTALS/f'pedestal-{iso}.npy'
    if not path.exists():
        return [NOMINAL_BLACK]*4, 'nominal 2048, no dark frames at this ISO'
    return [float(v) for v in np.median(np.load(path, allow_pickle=False), axis=0)], 'median visible pedestal of the training dark frames'


def optical_black(raw):
    """Mean of the masked left columns per canonical plane (diagnosis only)."""
    import numpy as np
    sizes = raw.sizes
    if sizes.top_margin % 2 or sizes.left_margin % 2 or sizes.left_margin < 32:
        raise ValueError('Even margins and a masked left area are required')
    masked = raw.raw_image[sizes.top_margin:sizes.top_margin+sizes.height//2*2, 8:sizes.left_margin-8].astype(np.float64)
    return [float(masked[y::2, x::2].mean()) for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))]


def select_frames(first, last, count):
    numbers = list(range(first, last+1))
    picks = sorted({numbers[0], numbers[len(numbers)//2], numbers[-1]})[:count]
    return [CARD/f'6V1A{n:04d}.CR3' for n in picks]


def crop_window(shape, shift):
    """Crop of the reference and the matching crop of the moving frame: reference[y, x] = moving[y - dy, x - dx]."""
    h, w = shape
    y0, x0 = (h-CROP[0])//2, (w-CROP[1])//2
    dy, dx = shift
    if not (0 <= y0-dy and y0-dy+CROP[0] <= h and 0 <= x0-dx and x0-dx+CROP[1] <= w):
        raise ValueError('Shift moves the crop outside the frame')
    return (slice(y0, y0+CROP[0]), slice(x0, x0+CROP[1])), (slice(y0-dy, y0-dy+CROP[0]), slice(x0-dx, x0-dx+CROP[1]))


def block_means(planes, block=BLOCK):
    c, h, w = planes.shape
    return planes[:, :h//block*block, :w//block*block].reshape(c, h//block, block, w//block, block).mean(axis=(2, 4))


def fit_mapping(noisy, reference, valid):
    """Per-plane gain/offset of noisy onto the reference, on fully valid 8x8 blocks of block means."""
    import numpy as np
    rows = []
    nb, rb = block_means(noisy.astype(np.float64)), block_means(reference.astype(np.float64))
    vb = block_means(valid.astype(np.float64)) == 1
    for plane in range(4):
        m = vb[plane]
        design = np.stack([nb[plane][m], np.ones(int(m.sum()))], axis=1)
        gain, offset = np.linalg.lstsq(design, rb[plane][m], rcond=None)[0]
        rows.append((float(gain), float(offset)))
    return rows


def score_method(estimate, noisy, reference, valid, mapping):
    import numpy as np
    inner = (slice(None), slice(BORDER, -BORDER), slice(BORDER, -BORDER))
    g = np.array([m[0] for m in mapping])[:, None, None]
    o = np.array([m[1] for m in mapping])[:, None, None]
    est, obs = (g*estimate.astype(np.float64)+o)[inner], (g*noisy.astype(np.float64)+o)[inner]
    ref, mask = reference.astype(np.float64)[inner], valid[inner]
    error, before = (est-ref)[mask], (obs-ref)[mask]
    mse, observed = float(np.mean(error**2)), float(np.mean(before**2))
    d = (obs-est)[mask]
    alpha = float(d@before/max(d@d, 1e-30)) if estimate is not noisy else 1.
    planes = [float((est-ref)[p][mask[p]].mean()) for p in range(4)]
    return dict(mse=mse, observed_mse=observed, psnr_gain_db=10*math.log10(observed/max(mse, 1e-30)),
                oracle_correction_alpha=alpha, mean_error_by_plane=planes)


def prepare(output, scene_set):
    import numpy as np
    import rawpy
    from real_noise_bench import phase_shift
    chosen = scene_sets()[scene_set]
    bank = read(BANK)
    output.mkdir(exist_ok=False)
    arrays, entries = {}, []
    references = {scene: load_reference(scene) for scene in sorted({b[0] for b in chosen['bursts']})}
    positions = ((0, 0), (0, 1), (1, 0), (1, 1))
    for scene, iso, first, last, tag in chosen['bursts']:
        law, provenance = noise_law(bank, iso)
        reference, reference_frames, reference_sigma, audit_path = references[scene]
        paths = select_frames(first, last, chosen['frames_per_burst'])
        black, black_provenance = dark_black(iso)
        frames, optical, clipped = [], [], None
        for path in paths:
            with rawpy.imread(str(path)) as raw:
                if raw.raw_colors_visible[:2, :2].tolist() != [[0, 1], [3, 2]]:
                    raise ValueError('RGGB R6 III phase required')
                optical.append(optical_black(raw))
                mosaic = raw.raw_image_visible.astype(np.float32)
                white = float(raw.white_level)
            mosaic = mosaic[:mosaic.shape[0]//2*2, :mosaic.shape[1]//2*2]
            planes = np.stack([(mosaic[y::2, x::2]-black[i])/DIVISOR for i, (y, x) in enumerate(positions)])
            near_white = np.stack([mosaic[y::2, x::2] >= white-64 for y, x in positions])
            clipped = near_white if clipped is None else clipped | near_white
            frames.append(planes)
        mean_green = np.mean([f[1:3].mean(axis=0) for f in frames], axis=0)
        h, w = reference.shape[-2:]
        centre = (slice(h//2-512, h//2+512), slice(w//2-768, w//2+768))
        shift, sharpness = phase_shift(reference[1:3].mean(axis=0)[centre], mean_green[centre])
        ref_crop, mov_crop = crop_window((h, w), shift)
        ref = reference[:, ref_crop[0], ref_crop[1]].astype(np.float32)
        valid = (ref < .9) & ~clipped[:, mov_crop[0], mov_crop[1]]
        key = f'{scene}-{iso}'+(f'-{tag}' if tag else '')
        arrays[key+'-reference'] = ref
        arrays[key+'-valid'] = valid
        arrays[key+'-noisy'] = np.stack([f[:, mov_crop[0], mov_crop[1]] for f in frames]).astype(np.float32)
        audited = audited_shift(scene, iso, first)
        entries.append(dict(key=key, scene=scene, iso=iso, tag=tag, burst_first=first, burst_last=last,
            plane_shape=[h, w], files=[p.name for p in paths],
            file_sha256=[digest(p) for p in paths], black_by_plane=black, black_provenance=black_provenance,
            optical_black_by_frame=optical, shift_packed_cells=shift,
            audited_burst_shift=audited, correlation_peak_sharpness=sharpness,
            noise_law=[dict(shot_slope_dn=s, read_variance_dn2=r) for s, r in law], noise_law_provenance=provenance,
            trained_iso=iso in TRAINED_ISO, valid_fraction=float(valid.mean()),
            reference_frames=reference_frames, reference_temporal_sigma_by_plane=reference_sigma,
            reference_audit=str(audit_path), reference_audit_sha256=digest(audit_path)))
    np.savez(output/'crops.npz', **arrays)
    write(output/'crops.json', dict(schema_version=2, status='prepared_real_noise_crops', scene_set=scene_set,
        frames_per_burst=chosen['frames_per_burst'], crop=list(CROP), divisor=DIVISOR,
        measured_at_utc=datetime.now(timezone.utc).isoformat(), entries=entries, crops_sha256=digest(output/'crops.npz'),
        bank_sha256=digest(BANK), tool_sha256=digest(Path(__file__))))


def burst(output):
    import numpy as np
    import rawpy
    crops = read(output/'crops.json')
    if digest(output/'crops.npz') != crops['crops_sha256']:
        raise ValueError('Crops changed')
    arrays, counts = {}, {}
    for entry in crops['entries']:
        black = entry['black_by_plane']
        _, mov_crop = crop_window(entry['plane_shape'], entry['shift_packed_cells'])
        total, count = None, 0
        for number in range(entry['burst_first'], entry['burst_last']+1):
            with rawpy.imread(str(CARD/f'6V1A{number:04d}.CR3')) as raw:
                mosaic = raw.raw_image_visible.astype(np.float32)
            mosaic = mosaic[:mosaic.shape[0]//2*2, :mosaic.shape[1]//2*2]
            planes = np.stack([(mosaic[y::2, x::2][mov_crop]-black[i])/DIVISOR
                               for i, (y, x) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1)))]).astype(np.float64)
            total = planes if total is None else total+planes
            count += 1
        arrays[entry['key']+'-sum'] = total
        counts[entry['key']] = count
    np.savez(output/'burst.npz', **arrays)
    write(output/'burst.json', dict(schema_version=1, status='burst_sums', counts=counts,
        burst_sha256=digest(output/'burst.npz'), crops_sha256=crops['crops_sha256'], tool_sha256=digest(Path(__file__))))


def same_burst_score(estimate, noisy, others_mean, count, law, valid):
    """MSE against the mean of the other frames of the burst, minus that mean's own noise from the law."""
    import numpy as np
    inner = (slice(None), slice(BORDER, -BORDER), slice(BORDER, -BORDER))
    mapping = fit_mapping(noisy, others_mean, valid)
    g = np.array([m[0] for m in mapping])[:, None, None]
    o = np.array([m[1] for m in mapping])[:, None, None]
    a = np.array([r['shot_slope_dn'] for r in law])[:, None, None]/DIVISOR
    b = np.array([r['read_variance_dn2'] for r in law])[:, None, None]/DIVISOR**2
    floor = float(((a*np.maximum(others_mean, 0)+b)/(count-1))[inner][valid[inner]].mean())
    ref, mask = others_mean[inner], valid[inner]
    before = float(np.mean(((g*noisy+o)[inner]-ref)[mask]**2))-floor
    after = float(np.mean(((g*estimate+o)[inner]-ref)[mask]**2))-floor
    return dict(floor_mse=floor, corrected_observed_mse=before, corrected_mse=after,
                psnr_gain_db=10*math.log10(max(before, 1e-30)/max(after, 1e-30)) if after > 0 else None)


def network(output, campaign, step=None):
    import numpy as np
    import torch
    from input_modes import load_trained_model
    from physical_natural_training import CANONICAL_TAGS, sigma_map
    crops = read(output/'crops.json')
    if digest(output/'crops.npz') != crops['crops_sha256']:
        raise ValueError('Crops changed')
    plan = read(campaign/'plan.json')
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    arrays = np.load(output/'crops.npz')
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    outputs, checkpoints = {}, {}
    for seed in plan['seeds']:
        model, checkpoint_sha256, checkpoint_step, _ = load_trained_model(campaign, plan, seed, step)
        checkpoints[str(seed)] = dict(sha256=checkpoint_sha256, step=checkpoint_step)
        with torch.inference_mode():
            for entry in crops['entries']:
                slope = torch.tensor([r['shot_slope_dn'] for r in entry['noise_law']], device='cuda')[None, :, None, None]
                readv = torch.tensor([r['read_variance_dn2'] for r in entry['noise_law']], device='cuda')[None, :, None, None]
                result = []
                for frame in arrays[entry['key']+'-noisy']:
                    x = torch.from_numpy(frame).cuda()[None]
                    sigma = sigma_map(x, slope, readv, DIVISOR)
                    a, b = slope/DIVISOR, readv/DIVISOR**2
                    result.append(model.correct(x, sigma, tags, a, b)['corrected'][0].cpu().numpy())
                outputs[f'{entry["key"]}-seed{seed}'] = np.stack(result).astype(np.float32)
        del model
        torch.cuda.empty_cache()
    np.savez(output/'network.npz', **outputs)
    write(output/'network.json', dict(schema_version=1, status='network_outputs', campaign=str(campaign.resolve()),
        input_mode=plan.get('input_mode', 'sigma_map'), seeds=plan['seeds'], checkpoints_sha256=checkpoints,
        network_sha256=digest(output/'network.npz'), crops_sha256=crops['crops_sha256'], tool_sha256=digest(Path(__file__))))


def bm3d_mode(output):
    import time
    import numpy as np
    import bm3d
    from physical_natural_comparators import anscombe, inverse_anscombe
    crops = read(output/'crops.json')
    if digest(output/'crops.npz') != crops['crops_sha256']:
        raise ValueError('Crops changed')
    arrays = np.load(output/'crops.npz')
    started = time.monotonic()
    outputs = {}
    for entry in crops['entries']:
        frames = arrays[entry['key']+'-noisy']
        estimate = np.zeros_like(frames)
        for index, frame in enumerate(frames):
            for plane, law in enumerate(entry['noise_law']):
                a, b = law['shot_slope_dn']/DIVISOR, law['read_variance_dn2']/DIVISOR**2
                z = anscombe(frame[plane].astype(np.float64), a, b)
                estimate[index, plane] = inverse_anscombe(np.asarray(bm3d.bm3d(z, sigma_psd=1.), np.float64), a, b)
        outputs[entry['key']] = estimate
    np.savez(output/'bm3d.npz', **outputs)
    write(output/'bm3d.json', dict(schema_version=1, status='bm3d_outputs', bm3d_version=getattr(bm3d, '__version__', 'unknown'),
        seconds=time.monotonic()-started, bm3d_sha256=digest(output/'bm3d.npz'), crops_sha256=crops['crops_sha256'],
        method='generalized Anscombe per plane with the entry law, bm3d(sigma_psd=1), closed-form unbiased inverse'))


def score(output):
    import numpy as np
    crops, net, ref_bm3d = read(output/'crops.json'), read(output/'network.json'), read(output/'bm3d.json')
    for name, meta, key in (('crops.npz', crops, 'crops_sha256'), ('network.npz', net, 'network_sha256'), ('bm3d.npz', ref_bm3d, 'bm3d_sha256')):
        if digest(output/name) != meta[key]:
            raise ValueError(name+' changed')
    arrays, networks, bm3ds = np.load(output/'crops.npz'), np.load(output/'network.npz'), np.load(output/'bm3d.npz')
    bursts, bursts_meta = None, None
    if (output/'burst.json').exists():
        bursts_meta = read(output/'burst.json')
        if digest(output/'burst.npz') != bursts_meta['burst_sha256'] or bursts_meta['crops_sha256'] != crops['crops_sha256']:
            raise ValueError('burst.npz changed or belongs to other crops')
        bursts = np.load(output/'burst.npz')
    rows, summary = [], []
    for entry in crops['entries']:
        key = entry['key']
        reference, valid, noisy = arrays[key+'-reference'], arrays[key+'-valid'], arrays[key+'-noisy']
        floor = [s*s/entry['reference_frames'] for s in entry['reference_temporal_sigma_by_plane']]
        frame_rows = []
        for index, frame in enumerate(noisy):
            mapping = fit_mapping(frame, reference, valid)
            methods = {'vst_bm3d': score_method(bm3ds[key][index], frame, reference, valid, mapping)}
            for seed in net['seeds']:
                methods[f'network_seed{seed}'] = score_method(networks[f'{key}-seed{seed}'][index], frame, reference, valid, mapping)
            if bursts is not None:
                count = bursts_meta['counts'][key]
                others = (bursts[key+'-sum']-frame.astype(np.float64))/(count-1)
                for name, estimate in [('vst_bm3d', bm3ds[key][index])]+[(f'network_seed{s}', networks[f'{key}-seed{s}'][index]) for s in net['seeds']]:
                    methods[name]['same_burst'] = same_burst_score(estimate.astype(np.float64), frame.astype(np.float64),
                                                                   others, count, entry['noise_law'], valid)
            observed = methods['vst_bm3d']['observed_mse']
            # Reference noise floor in reference units (mapping brings estimates onto the reference scale).
            floor_mse = float(np.mean(floor))
            frame_rows.append(dict(file=entry['files'][index], mapping=[dict(gain=g, offset=o) for g, o in mapping],
                reference_floor_mse=floor_mse, measurable_gain_ceiling_db=10*math.log10(observed/max(floor_mse, 1e-30)),
                methods=methods))
        entry_summary = dict(key=key, scene=entry['scene'], iso=entry['iso'], trained_iso=entry['trained_iso'],
            noise_law_provenance=entry['noise_law_provenance'], shift_packed_cells=entry['shift_packed_cells'],
            measurable_gain_ceiling_db=float(np.median([r['measurable_gain_ceiling_db'] for r in frame_rows])), methods={})
        for method in frame_rows[0]['methods']:
            entry_summary['methods'][method] = dict(
                median_gain_db=float(np.median([r['methods'][method]['psnr_gain_db'] for r in frame_rows])),
                median_oracle_alpha=float(np.median([r['methods'][method]['oracle_correction_alpha'] for r in frame_rows])),
                max_abs_plane_bias=float(max(abs(v) for r in frame_rows for v in r['methods'][method]['mean_error_by_plane'])))
            if bursts is not None:
                same = [r['methods'][method]['same_burst']['psnr_gain_db'] for r in frame_rows]
                entry_summary['methods'][method]['same_burst_median_gain_db'] = float(np.median([v for v in same if v is not None])) if any(v is not None for v in same) else None
            if method != 'vst_bm3d':
                entry_summary['methods'][method]['median_minus_bm3d_db'] = float(np.median(
                    [r['methods'][method]['psnr_gain_db']-r['methods']['vst_bm3d']['psnr_gain_db'] for r in frame_rows]))
        summary.append(entry_summary)
        rows.append(dict(key=key, frames=frame_rows))
    result = dict(schema_version=1, status='real_noise_evaluation_development_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), crops=crops, network=net, bm3d=ref_bm3d,
        tool_sha256=digest(Path(__file__)), summary=summary, rows=rows,
        limits=[f"Scene set {crops.get('scene_set', 'interior')}, {crops.get('frames_per_burst', 3)} frames per burst, one central crop",
                'Reference is an ISO100 burst mean: its own noise is the reported floor, not removed from the scores',
                'Per-frame gain/offset fitted on noisy block means only; local motion or light change not corrected',
                'ISO 1600 and 12800 are outside the training ISO; the 12800 shot slope is interpolated',
                'Same-burst reference: floor from the measured law, fixed-pattern noise shared by the burst kept in the reference',
                'Shifts are integer packed cells from phase correlation; subcell misregistration counts as error for every method'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('prepare', 'burst', 'network', 'bm3d', 'score'), required=True)
    parser.add_argument('--campaign', type=Path, default=BUILD/'sigma-input-training-v001')
    parser.add_argument('--scene-set', choices=('interior', 'color_sheet'), default='interior')
    parser.add_argument('--checkpoint-step', type=int, help='network mode: a planned intermediate checkpoint instead of the final one')
    args = parser.parse_args()
    output = args.output.resolve()
    if args.mode == 'prepare':
        prepare(output, args.scene_set)
    elif args.mode == 'burst':
        burst(output)
    elif args.mode == 'network':
        network(output, args.campaign.resolve(), args.checkpoint_step)
    elif args.mode == 'bm3d':
        bm3d_mode(output)
    else:
        score(output)
