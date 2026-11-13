"""The beta-1 U-Net on EOS 60D RAW, with the qualified noise law and with the law estimated from
each RAW alone (estimate_single_raw_law, on arrays).

Usage: python denoise_estimated_against_qualified.py <campaign folder> <qualification.json>
       <iso map.json> <output.json> <cr2>...
The campaign folder holds plan.json and seed*/resume.pt (final-beta1-training-v002). The ISO map
gives [model, ISO] per photo path, read beforehand from the EXIF (the training environment has
no Pillow). Needs torch with CUDA and rawpy; not a unit test.

A. Synthetic on real: ISO 100 photos as reference, the noise the qualified law adds up to ISO T
   (800 to 6400) drawn per pixel (Poisson-Gauss, Gaussian approximation), masked columns
   included; the estimator reads the noisy RAW. PSNR gain over the noisy input, both laws.
B. Real ISO 100 photos: what each law removes (input minus output), estimated over qualified.
C. Real photos at other ISO: RMS difference of the two outputs, in units of the qualified noise.
Crops: three of 512 x 512 per plane (1024 pixels), centre and two diagonals.
"""
import json
import math
import sys
import time
from pathlib import Path

import numpy as np
import rawpy
import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from input_modes import load_trained_model  # noqa: E402
from physical_natural_training import CANONICAL_TAGS, sigma_map  # noqa: E402

DIVISOR = 12303.0              # packaging/neural-model.json input_divisor, as production
BLOCK, QUANTILE = 16, 0.1
CROP = 512
# 60D visible mosaic: rows G2 B / R G. One row down gives R G / G2 B (RGGB). Canonical planes
# R, G1, G2, B are then the raster positions (1,0), (1,1), (0,0), (0,1) of the visible mosaic.
CANONICAL_FROM_RASTER = (2, 3, 0, 1)
_bias = {}


def quantile_bias(q):
    if q not in _bias:
        rng = np.random.default_rng(1)
        x = rng.standard_normal((4000, BLOCK + 2, BLOCK + 2))
        r = x[:, 1:-1, 1:-1] - 0.25 * (x[:, :-2, 1:-1] + x[:, 2:, 1:-1] + x[:, 1:-1, :-2] + x[:, 1:-1, 2:])
        v = (np.median(np.abs(r - np.median(r, axis=(1, 2), keepdims=True)), axis=(1, 2)) / 0.6745) ** 2 / 1.25
        _bias[q] = float(np.quantile(v, q))
    return _bias[q]


def estimate(full, visible_rows, top, left, white, width):
    """estimate_single_raw_law on arrays: [(a, b)] per raster position of the visible mosaic."""
    law = []
    for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1)):
        masked = full[top + dy:top + visible_rows:2, 8 + dx:left - 8:2]
        black = float(np.median(masked))
        cols = masked - np.median(masked, axis=0, keepdims=True)
        b = float((np.median(np.abs(cols - np.median(cols))) / 0.6745) ** 2)
        plane = full[top + dy:top + visible_rows:2, left + dx:left + width:2] - black
        r = plane[1:-1, 1:-1] - 0.25 * (plane[:-2, 1:-1] + plane[2:, 1:-1] + plane[1:-1, :-2] + plane[1:-1, 2:])
        h, w = (r.shape[0] // BLOCK) * BLOCK, (r.shape[1] // BLOCK) * BLOCK
        cut = lambda x: x[:h, :w].reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK).swapaxes(1, 2).reshape(-1, BLOCK * BLOCK)
        rb, mb = cut(r), cut(plane[1:1 + h, 1:1 + w])
        var = (np.median(np.abs(rb - np.median(rb, axis=1, keepdims=True)), axis=1) / 0.6745) ** 2 / 1.25
        mean, peak = mb.mean(axis=1), mb.max(axis=1)
        keep = (peak < 0.95 * (white - black)) & (mean > 0)
        edges = np.unique(np.quantile(mean[keep], np.linspace(0, 1, 25)))
        est = []
        for lo, hi in zip(edges[:-1], edges[1:]):
            sel = keep & (mean >= lo) & (mean < hi)
            if sel.sum() >= 40:
                est.append((float(np.quantile(var[sel], QUANTILE)) / quantile_bias(QUANTILE) - b) / float(np.median(mean[sel])))
        est = np.sort(est)
        law.append((float(np.mean(est[:max(1, len(est) // 2)])), b))
    return law


def read_raw(path):
    with rawpy.imread(str(path)) as raw:
        full = raw.raw_image.astype(np.float64)
        top, left, white = raw.sizes.top_margin, raw.sizes.left_margin, float(raw.white_level)
        rows, width = raw.raw_image_visible.shape
    return full, top, left, white, rows, width


def blacks(full, top, left, rows):
    return [float(np.median(full[top + dy:top + rows:2, 8 + dx:left - 8:2])) for dy, dx in ((0, 0), (0, 1), (1, 0), (1, 1))]


def canonical_planes(full, top, left, rows, width, black):
    """R, G1, G2, B planes in network units (DN minus black, over DIVISOR), RGGB phase."""
    mosaic = full[top + 1:top + rows, left:left + width]
    mosaic = mosaic[:mosaic.shape[0] // 2 * 2, :mosaic.shape[1] // 2 * 2]
    out = []
    for i, (y, x) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
        raster = CANONICAL_FROM_RASTER[i]
        out.append((mosaic[y::2, x::2] - black[raster]) / DIVISOR)
    return np.stack(out).astype(np.float32)


def canonical_law(law):
    return [law[r] for r in CANONICAL_FROM_RASTER]


def crops_of(shape):
    h, w = shape
    return [(h // 2 - CROP // 2, w // 2 - CROP // 2), (h // 4 - CROP // 2, w // 4 - CROP // 2),
            (3 * h // 4 - CROP // 2, 3 * w // 4 - CROP // 2)]


def denoise(model, tags, planes, law):
    slope = torch.tensor([a for a, _ in law], device='cuda', dtype=torch.float32)[None, :, None, None]
    readv = torch.tensor([b for _, b in law], device='cuda', dtype=torch.float32)[None, :, None, None]
    with torch.inference_mode():
        x = torch.from_numpy(np.ascontiguousarray(planes)).cuda()[None]
        sigma = sigma_map(x, slope, readv, DIVISOR)
        return model.correct(x, sigma, tags, slope / DIVISOR, readv / DIVISOR ** 2)['corrected'][0].cpu().numpy()


def psnr(estimate, reference):
    return float(10 * math.log10(1.0 / max(float(np.mean((estimate - reference) ** 2)), 1e-20)))


ISO_MAP = {}


def iso_of(path):
    """Model and ISO, read beforehand from the EXIF (this environment has no Pillow)."""
    return tuple(ISO_MAP[Path(path).as_posix()])


def law_at(qualified, iso):
    isos = sorted(int(k) for k in qualified['isos'])
    at = lambda i: [(p['fit_dark_read']['shot_slope_dn'], p['fit_dark_read']['read_variance_dn2'])
                    for p in qualified['isos'][str(i)]['positions']]
    if iso in isos:
        return at(iso)
    if iso < isos[0] or iso > isos[-1]:
        return None
    lo, hi = max(i for i in isos if i < iso), min(i for i in isos if i > iso)
    t = (math.log(iso) - math.log(lo)) / (math.log(hi) - math.log(lo))
    return [(math.exp((1 - t) * math.log(a0) + t * math.log(a1)), math.exp((1 - t) * math.log(b0) + t * math.log(b1)))
            for (a0, b0), (a1, b1) in zip(at(lo), at(hi))]


def main(campaign, qualified_path, iso_map, output, *photos):
    campaign = Path(campaign)
    if Path(output).exists():
        raise SystemExit(f'{output} exists: a new version gets a new name')
    started = time.time()
    ISO_MAP.update(json.load(open(iso_map, encoding='utf-8')))
    qualified = json.load(open(qualified_path, encoding='utf-8'))
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    plan = json.load(open(campaign/'plan.json'))
    model, sha, step, _ = load_trained_model(campaign, plan, plan['seeds'][0], None)
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    rng = np.random.default_rng(20261113)
    synthetic, real100, real_other = [], [], []
    for path in photos:
        model_name, iso = iso_of(path)
        if '60D' not in model_name:
            continue
        full, top, left, white, rows, width = read_raw(path)
        black = blacks(full, top, left, rows)
        if iso == 100:
            clean = canonical_planes(full, top, left, rows, width, black)
            base = law_at(qualified, 100)
            # B. the real ISO 100 photo itself.
            cal, est = canonical_law(base), canonical_law(estimate(full, rows, top, left, white, width))
            removed = []
            for y, x in crops_of(clean.shape[1:]):
                crop = clean[:, y:y + CROP, x:x + CROP]
                oc, oe = denoise(model, tags, crop, cal), denoise(model, tags, crop, est)
                removed.append(dict(calibrated=float(np.mean((crop - oc) ** 2)), estimated=float(np.mean((crop - oe) ** 2)),
                                    outputs_rms_difference=float(np.sqrt(np.mean((oe - oc) ** 2)))))
            ratio = float(np.median([r['estimated'] / r['calibrated'] for r in removed]))
            real100.append(dict(file=Path(path).name, estimated_slope_over_qualified=[e[0] / c[0] for e, c in zip(est, cal)],
                                removed_energy_ratio=ratio, crops=removed))
            print('B', Path(path).name, 'removed est/cal %.2f' % ratio, flush=True)
            # A. synthetic noise up to higher ISO.
            for target in (800, 1600, 3200, 6400):
                law_t = law_at(qualified, target)
                noisy_full = full.copy()
                for p, (dy, dx) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
                    (a0, b0), (a1, b1) = base[p], law_t[p]
                    region = noisy_full[top + dy:top + rows:2, dx::2]
                    signal = np.maximum(region - black[p], 0)
                    extra = np.maximum((a1 - a0) * signal + (b1 - b0), 0)
                    region += rng.standard_normal(region.shape) * np.sqrt(extra)
                    noisy_full[top + dy:top + rows:2, dx::2] = region
                noisy = canonical_planes(noisy_full, top, left, rows, width, black)
                est_t = estimate(noisy_full, rows, top, left, white, width)
                cal_t = canonical_law(law_t)
                gains = dict(calibrated=[], estimated=[], estimated_slope_x1_2=[])
                for y, x in crops_of(clean.shape[1:]):
                    ref, inp = clean[:, y:y + CROP, x:x + CROP], noisy[:, y:y + CROP, x:x + CROP]
                    base_psnr = psnr(inp, ref)
                    for name, law in (('calibrated', cal_t), ('estimated', canonical_law(est_t)),
                                      ('estimated_slope_x1_2', canonical_law([(a * 1.2, b) for a, b in est_t]))):
                        gains[name].append(psnr(denoise(model, tags, inp, law), ref) - base_psnr)
                row = dict(file=Path(path).name, target_iso=target,
                           estimated_slope_over_qualified=[e[0] / c[0] for e, c in zip(est_t, law_t)],
                           gain_db={k: float(np.median(v)) for k, v in gains.items()})
                synthetic.append(row)
                print('A', Path(path).name, target, 'a/a0 %.2f' % np.median(row['estimated_slope_over_qualified']),
                      ' '.join('%s %.2f' % kv for kv in row['gain_db'].items()), flush=True)
        else:
            law = law_at(qualified, iso)
            if law is None:
                continue
            planes = canonical_planes(full, top, left, rows, width, black)
            cal, est = canonical_law(law), canonical_law(estimate(full, rows, top, left, white, width))
            diffs = []
            for y, x in crops_of(planes.shape[1:]):
                crop = planes[:, y:y + CROP, x:x + CROP]
                oc, oe = denoise(model, tags, crop, cal), denoise(model, tags, crop, est)
                sigma = np.sqrt(np.mean([a * max(float(crop[i].mean()) * DIVISOR, 0) + b for i, (a, b) in enumerate(cal)])) / DIVISOR
                diffs.append(float(np.sqrt(np.mean((oe - oc) ** 2)) / sigma))
            real_other.append(dict(file=Path(path).name, iso=iso,
                                   estimated_slope_over_qualified=[e[0] / c[0] for e, c in zip(est, cal)],
                                   outputs_difference_over_noise_sigma=float(np.median(diffs))))
            print('C', Path(path).name, iso, 'diff/sigma %.3f' % np.median(diffs), flush=True)
    summary = {}
    for target in (800, 1600, 3200, 6400):
        rows_t = [r for r in synthetic if r['target_iso'] == target]
        if rows_t:
            d = [r['gain_db']['estimated'] - r['gain_db']['calibrated'] for r in rows_t]
            d12 = [r['gain_db']['estimated_slope_x1_2'] - r['gain_db']['calibrated'] for r in rows_t]
            summary[f'synthetic_{target}'] = dict(photos=len(rows_t), calibrated_gain_median_db=float(np.median([r['gain_db']['calibrated'] for r in rows_t])),
                estimated_minus_calibrated_median_db=float(np.median(d)), worst_db=float(min(d)),
                estimated_x1_2_minus_calibrated_median_db=float(np.median(d12)), worst_x1_2_db=float(min(d12)),
                slope_ratio_median=float(np.median([np.median(r['estimated_slope_over_qualified']) for r in rows_t])))
    if real100:
        summary['real_iso100_removed_energy_ratio'] = dict(median=float(np.median([r['removed_energy_ratio'] for r in real100])),
                                                          max=float(max(r['removed_energy_ratio'] for r in real100)))
    if real_other:
        summary['real_other_outputs_difference_over_sigma'] = dict(median=float(np.median([r['outputs_difference_over_noise_sigma'] for r in real_other])),
                                                                  max=float(max(r['outputs_difference_over_noise_sigma'] for r in real_other)))
    result = dict(schema_version=1, status='exploratory_denoise_estimated_against_qualified_law', camera='Canon EOS 60D',
                  method=__doc__.strip(), model_step=step, checkpoint_sha256=sha, campaign=campaign.name, divisor=DIVISOR,
                  qualified_law=Path(qualified_path).name, crop_per_plane=CROP, seed=20261113, summary=summary,
                  synthetic=synthetic, real_iso100=real100, real_other=real_other, seconds=time.time() - started)
    Path(output).write_text(json.dumps(result, indent=1), encoding='utf-8')
    print(json.dumps(summary, indent=1))


if __name__ == '__main__':
    main(*sys.argv[1:])
