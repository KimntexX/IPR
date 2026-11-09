"""Where the network loses to VST + BM3D on the development tiles: anatomy of the error.

Runs in the external BM3D environment (numpy + bm3d, never shipped). Reads the
exported comparator arrays, recomputes the per-plane VST + BM3D estimates, then
splits each estimator's error e = estimate - clean against the added noise
n = noisy - clean: e = beta*n + r, where beta*n is the noise left in place and r
the part uncorrelated with that noise (lost or invented detail, bias, blotches).
An oracle scalar alpha on the correction (noisy - estimate), fitted against the
clean tile, tells whether the estimator found the noise but applied too little.
The error power is also distributed over radial frequency bands of the packed
planes, and the green-pair error correlation flags low-frequency colour blotches.
Development tiles only: diagnosis, not a qualification.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time

from physical_natural_comparators import BORDER, anscombe, inverse_anscombe
from prepare_next_training import digest, read, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
# Radial frequency bands in cycles per packed cell (Nyquist 0.5).
BANDS = ((0., 1/32), (1/32, 1/16), (1/16, 1/8), (1/8, 1/4), (1/4, .5001))


def inner(array):
    return array[..., BORDER:-BORDER, BORDER:-BORDER]


def noise_split(error, noise):
    """beta, residual-noise power and remaining power per sample: e = beta*n + r with r orthogonal to n."""
    import numpy as np
    e, n = error.astype(np.float64).ravel(), noise.astype(np.float64).ravel()
    beta = float(e@n/max(n@n, 1e-30))
    rest = e-beta*n
    return dict(beta=beta, noise_power=float(beta*beta*(n@n)/n.size), rest_power=float(rest@rest/n.size),
                total_power=float(e@e/n.size))


def band_fractions(planes):
    """Share of the power of (planes, h, w) in each radial band, Hann window, mean removed per plane."""
    import numpy as np
    h, w = planes.shape[-2:]
    window = np.outer(np.hanning(h), np.hanning(w))
    spectrum = np.abs(np.fft.fft2((planes-planes.mean(axis=(-2, -1), keepdims=True))*window))**2
    fy, fx = np.meshgrid(np.fft.fftfreq(h), np.fft.fftfreq(w), indexing='ij')
    radius = np.hypot(fy, fx)
    power = spectrum.sum(axis=0)
    total = max(float(power.sum()), 1e-30)
    return [float(power[(radius >= lo) & (radius < hi)].sum()/total) for lo, hi in BANDS]


def oracle_amplitude(estimate, clean, noisy):
    """Least-squares alpha with noisy - alpha*(noisy - estimate) closest to clean, and the gain it would give."""
    import numpy as np
    d = inner(noisy-estimate).astype(np.float64).ravel()
    y = inner(noisy-clean).astype(np.float64).ravel()
    alpha = float(d@y/max(d@d, 1e-30))
    rest = y-alpha*d
    return alpha, 10*math.log10((y@y)/max(rest@rest, 1e-30))


def green_correlation(error):
    import numpy as np
    a, b = error[1].ravel().astype(np.float64), error[2].ravel().astype(np.float64)
    a, b = a-a.mean(), b-b.mean()
    return float(a@b/max(math.sqrt((a@a)*(b@b)), 1e-30))


def describe(estimate, clean, noisy):
    import numpy as np
    error, noise = inner(estimate-clean), inner(noisy-clean)
    split = noise_split(error, noise)
    before = float(np.square(noise, dtype=np.float64).mean())
    alpha, alpha_gain = oracle_amplitude(estimate, clean, noisy)
    return dict(oracle_correction_alpha=alpha, oracle_alpha_gain_db=alpha_gain,
                psnr_gain_db=10*math.log10(before/max(split['total_power'], 1e-30)), **split,
                noise_share=split['noise_power']/max(split['total_power'], 1e-30),
                error_bands=band_fractions(error), green_pair_error_correlation=green_correlation(error),
                mean_error=float(error.mean()))


def tile_content(clean):
    """Signal level and texture of the clean tile: mean, and Laplacian energy per plane."""
    import numpy as np
    c = inner(clean).astype(np.float64)
    lap = 4*c[:, 1:-1, 1:-1]-c[:, :-2, 1:-1]-c[:, 2:, 1:-1]-c[:, 1:-1, :-2]-c[:, 1:-1, 2:]
    return dict(mean=float(c.mean()), laplacian_rms=float(np.sqrt(np.square(lap).mean())),
                clean_bands=band_fractions(c))


def bm3d_estimates(noisy, laws):
    import numpy as np
    import bm3d
    estimate = np.zeros_like(noisy)
    for index in range(noisy.shape[0]):
        for iso_index in range(noisy.shape[1]):
            for plane in range(4):
                a, b = laws[iso_index, plane]
                z = anscombe(noisy[index, iso_index, plane].astype(np.float64), a, b)
                estimate[index, iso_index, plane] = inverse_anscombe(np.asarray(bm3d.bm3d(z, sigma_psd=1.), np.float64), a, b)
    return estimate


def run(comparators, output):
    import numpy as np
    exported = read(comparators/'export.json')
    if digest(comparators/'arrays.npz') != exported['arrays_sha256']:
        raise ValueError('Exported arrays changed')
    output.mkdir(exist_ok=False)
    arrays = np.load(comparators/'arrays.npz')
    clean, noisy, laws = arrays['clean'], arrays['noisy'], arrays['laws']
    network = arrays['network_seed71']
    started = time.monotonic()
    bm3d = bm3d_estimates(noisy, laws)
    np.save(output/'bm3d.npy', bm3d, allow_pickle=False)
    rows = []
    for index in range(noisy.shape[0]):
        content = tile_content(clean[index])
        for iso_index, iso in enumerate(exported['isos']):
            truth, observed = clean[index], noisy[index, iso_index]
            rows.append(dict(target_index=index, source=0 if index < 9 else 1, iso=iso, content=content,
                             noise_bands=band_fractions(inner(observed-truth)),
                             network=describe(network[index, iso_index], truth, observed),
                             vst_bm3d=describe(bm3d[index, iso_index], truth, observed)))
    summary = []
    for source in (0, 1):
        for iso in exported['isos']:
            picked = [r for r in rows if r['source'] == source and r['iso'] == iso]
            entry = dict(source=source, iso=iso)
            for method in ('network', 'vst_bm3d'):
                entry[method] = {key: float(np.median([r[method][key] for r in picked]))
                                 for key in ('psnr_gain_db', 'beta', 'noise_share', 'total_power', 'noise_power',
                                             'rest_power', 'green_pair_error_correlation',
                                             'oracle_correction_alpha', 'oracle_alpha_gain_db')}
                entry[method]['error_bands'] = np.median([r[method]['error_bands'] for r in picked], axis=0).tolist()
            entry['content_laplacian_rms'] = float(np.median([r['content']['laplacian_rms'] for r in picked]))
            summary.append(entry)
    result = dict(schema_version=1, status='development_error_anatomy_not_qualification',
        measured_at_utc=datetime.now(timezone.utc).isoformat(), comparators=str(comparators.resolve()),
        arrays_sha256=exported['arrays_sha256'], bm3d_sha256=digest(output/'bm3d.npy'),
        tool_sha256=digest(Path(__file__)), bm3d_seconds=time.monotonic()-started,
        bands_cycles_per_packed_cell=[list(b) for b in BANDS], summary=summary, rows=rows,
        limits=['18 development tiles of two sources, already inspected; diagnosis only',
                'beta*n is the part of the error correlated with this noise realisation; r mixes lost detail, bias and invented structure',
                'The oracle alpha uses the clean tile: a diagnosis of amplitude, not a usable correction',
                'Targets are ISO100 proxies with their own small noise, counted in r for every method'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparators', type=Path, default=BUILD/'physical-natural-comparators-v003')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.comparators.resolve(), args.output.resolve())
