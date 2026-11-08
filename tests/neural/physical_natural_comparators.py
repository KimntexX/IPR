"""Plan A comparators on the exact development realisations: network vs VST + BM3D per plane.

`export` (CUDA venv) regenerates the fixed development observations of plan A,
checks that the saved network reproduces its logged scores, and writes the
arrays. `bm3d` (external reference environment, never shipped) denoises each
canonical plane after a generalized Anscombe transform with the plane's own
Poisson-Gaussian law, inverts with the closed-form unbiased approximation, and
scores everything with the same metric as the training controls.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
BORDER = 32


def anscombe(y, a, b):
    """Generalized Anscombe for variance a*mean+b: unit noise in the transformed domain."""
    import numpy as np
    return 2/a*np.sqrt(np.maximum(a*y+3/8*a*a+b, 0))


def inverse_anscombe(d, a, b):
    """Closed-form approximation of the exact unbiased inverse (Makitalo and Foi, 2011/2013)."""
    import numpy as np
    d = np.maximum(d, 1e-6)
    s = math.sqrt(1.5)
    unit = .25*d*d+.25*s/d-11/8/(d*d)+5/8*s/d**3-1/8-b/(a*a)
    return a*np.maximum(unit, 0)


def score(estimate, clean, noisy):
    import numpy as np
    inner = (Ellipsis, slice(BORDER, -BORDER), slice(BORDER, -BORDER))
    error = (estimate-clean)[inner]
    before = float(np.square((noisy-clean)[inner], dtype=np.float64).mean())
    mse = float(np.square(error, dtype=np.float64).mean())
    return dict(mse=mse, observed_mse=before, psnr_gain_db=10*math.log10(before/max(mse, 1e-30)),
                mean_error_by_plane=error.reshape(4, -1).mean(axis=1).tolist())


def export(campaign, output):
    import numpy as np
    import torch
    import sys
    from dru_cfa import DruCfaCorrector
    from physical_natural_training import CANONICAL_TAGS, NoiseModel
    plan, data = read(campaign/'plan.json'), read(campaign/'data.json')
    for name, expected in data['files'].items():
        if digest(campaign/name) != expected:
            raise ValueError('Plan A input changed: '+name)
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    sys.path.insert(0, str(BUILD/'third-party-drunet-v001'))
    from models.network_unet import UNetRes
    development = torch.from_numpy(np.load(campaign/'development.npy', allow_pickle=False)).cuda()
    noise = NoiseModel(campaign, plan, 'cuda')
    tags = torch.tensor([CANONICAL_TAGS], device='cuda')
    output.mkdir(exist_ok=False)
    isos = plan['training_iso']
    shape = (len(development), len(isos), 4, plan['packed_side'], plan['packed_side'])
    noisy_all, sigma_all = np.zeros(shape, np.float32), np.zeros(shape, np.float32)
    outputs, reproduction = {}, []
    for seed in plan['seeds']:
        state = torch.load(campaign/f'seed{seed}/resume.pt', map_location='cuda', weights_only=False)
        if state['completed_steps'] != plan['steps']:
            raise ValueError('Final checkpoint required')
        core = UNetRes(in_nc=8, out_nc=4, nc=[32, 64, 128, 256], nb=4, act_mode='R',
                      downsample_mode='strideconv', upsample_mode='convtranspose')
        model = DruCfaCorrector(core).cuda()
        model.load_state_dict(state['model_state'])
        model.eval()
        logged = {(r['target_index'], r['iso']): r for r in read(campaign/f'seed{seed}/progress.json')['controls'][-1]['rows']}
        result = np.zeros(shape, np.float32)
        with torch.inference_mode():
            for index, target in enumerate(development):
                for iso_index, iso in enumerate(isos):
                    g = torch.Generator(device='cuda').manual_seed(9101000+index*8+iso_index)
                    clean = target.unsqueeze(0)
                    dark = noise.dark(iso_index, 'control', index).unsqueeze(0)
                    noisy, sigma = noise.sample(clean, torch.tensor([iso_index], device='cuda'), dark, g)
                    corrected = model.correct_observations(noisy, sigma, tags)
                    noisy_all[index, iso_index] = noisy[0].cpu().numpy()
                    sigma_all[index, iso_index] = sigma[0].cpu().numpy()
                    result[index, iso_index] = corrected[0].cpu().numpy()
                    mse = float((corrected-clean)[..., BORDER:-BORDER, BORDER:-BORDER].square().mean())
                    reproduction.append(abs(mse-logged[(index, iso)]['corrected_mse'])/logged[(index, iso)]['corrected_mse'])
        outputs[f'network_seed{seed}'] = result
    if max(reproduction) > 1e-4:
        raise ValueError(f'Saved network does not reproduce its logged scores ({max(reproduction):.2e})')
    laws = np.array([[[r['shot_slope_dn']/noise.divisor, r['read_variance_dn2']/noise.divisor**2]
                      for p in range(4) for r in plan['noise_parameters'] if r['iso'] == iso and r['plane'] == p]
                     for iso in isos], np.float64)
    np.savez(output/'arrays.npz', clean=development.cpu().numpy(), noisy=noisy_all, sigma=sigma_all, laws=laws, **outputs)
    write(output/'export.json', dict(status='exported_development_realisations', campaign=str(campaign),
        plan_sha256=digest(campaign/'plan.json'), isos=isos, seeds=plan['seeds'],
        reproduction_max_relative_mse_error=max(reproduction), arrays_sha256=digest(output/'arrays.npz'),
        tool_sha256=digest(Path(__file__))))


def compare(output):
    import numpy as np
    import bm3d
    exported = read(output/'export.json')
    if digest(output/'arrays.npz') != exported['arrays_sha256']:
        raise ValueError('Exported arrays changed')
    arrays = np.load(output/'arrays.npz')
    clean, noisy, laws = arrays['clean'], arrays['noisy'], arrays['laws']
    started = time.monotonic()
    estimate = np.zeros_like(noisy)
    for index in range(noisy.shape[0]):
        for iso_index in range(noisy.shape[1]):
            for plane in range(4):
                a, b = laws[iso_index, plane]
                z = anscombe(noisy[index, iso_index, plane].astype(np.float64), a, b)
                d = bm3d.bm3d(z, sigma_psd=1.)
                estimate[index, iso_index, plane] = inverse_anscombe(np.asarray(d, np.float64), a, b)
    elapsed = time.monotonic()-started
    rows = []
    for index in range(noisy.shape[0]):
        for iso_index, iso in enumerate(exported['isos']):
            row = dict(target_index=index, source=0 if index < 9 else 1, iso=iso)
            truth, observed = clean[index].astype(np.float64), noisy[index, iso_index].astype(np.float64)
            row['vst_bm3d'] = score(estimate[index, iso_index].astype(np.float64), truth, observed)
            for seed in exported['seeds']:
                row[f'network_seed{seed}'] = score(arrays[f'network_seed{seed}'][index, iso_index].astype(np.float64), truth, observed)
            rows.append(row)
    summary = []
    for iso in exported['isos']:
        selected = [r for r in rows if r['iso'] == iso]
        entry = dict(iso=iso)
        for method in ['vst_bm3d']+[f'network_seed{s}' for s in exported['seeds']]:
            entry[method] = dict(median_gain_db=float(np.median([r[method]['psnr_gain_db'] for r in selected])),
                by_source=[float(np.median([r[method]['psnr_gain_db'] for r in selected if r['source'] == s])) for s in (0, 1)],
                max_abs_plane_bias=float(max(abs(v) for r in selected for v in r[method]['mean_error_by_plane'])))
        for seed in exported['seeds']:
            delta = [r[f'network_seed{seed}']['psnr_gain_db']-r['vst_bm3d']['psnr_gain_db'] for r in selected]
            entry[f'network_seed{seed}_minus_bm3d_db'] = dict(median=float(np.median(delta)),
                by_source=[float(np.median([d for d, r in zip(delta, selected) if r['source'] == s])) for s in (0, 1)])
        summary.append(entry)
    result = dict(schema_version=1, status='development_comparison_not_final_test',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), export=exported,
        bm3d_version=getattr(bm3d, '__version__', 'unknown'), numpy=np.__version__,
        method=dict(vst='generalized Anscombe per canonical plane with its measured a,b in normalized units',
                    denoiser='bm3d.bm3d(sigma_psd=1), default profile, each plane separately',
                    inverse='closed-form approximation of the exact unbiased inverse',
                    metric='MSE inside a 32-cell border vs the ISO100 proxy, gain over the noisy input'),
        bm3d_seconds=elapsed, summary=summary, rows=rows,
        limits=['Development patches already inspected; two sources only; no final test',
                'BM3D per plane ignores cross-plane correlation; external non-commercial reference, not shipped',
                'Proxy targets keep their own ISO100 noise',
                'Classical IPR comparator needs a common RGB domain and is not part of this comparison',
                'Networks trained 2048 updates only, not converged'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-physical-natural-comparators-v001-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign', type=Path, default=BUILD/'physical-natural-training-v002')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('export', 'bm3d'), required=True)
    args = parser.parse_args()
    if args.mode == 'export':
        export(args.campaign.resolve(), args.output.resolve())
    else:
        compare(args.output.resolve())
