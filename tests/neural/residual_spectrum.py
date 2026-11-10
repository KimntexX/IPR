"""Radial spectrum of the error left by each denoiser on the real interior scenes.

Reads a completed real-noise evaluation (crops, network and BM3D outputs, burst
sums). Reference: mean of the other frames of the same burst, frame mapped on
it per plane. For each radial band, the power of (estimate - reference) is
divided by the power of (noisy - reference): 1 means the band is untouched,
the reference's own noise sets a floor near 1/(N - 1). Bands where the ratio
exceeds 1 mean structure or texture invented or removed. Diagnosis only.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from development_error_anatomy import BANDS
from prepare_next_training import digest, read, write
from real_noise_evaluation import BORDER, fit_mapping


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'


def band_power(planes):
    """Absolute power per radial band of (planes, h, w), Hann window, mean removed per plane."""
    import numpy as np
    h, w = planes.shape[-2:]
    window = np.outer(np.hanning(h), np.hanning(w))
    spectrum = np.abs(np.fft.fft2((planes-planes.mean(axis=(-2, -1), keepdims=True))*window))**2
    fy, fx = np.meshgrid(np.fft.fftfreq(h), np.fft.fftfreq(w), indexing='ij')
    radius = np.hypot(fy, fx)
    power = spectrum.sum(axis=0)
    return [float(power[(radius >= lo) & (radius < hi)].sum()) for lo, hi in BANDS]


def run(evaluation, output):
    import numpy as np
    crops, net, bm3d_meta, burst_meta = (read(evaluation/n) for n in ('crops.json', 'network.json', 'bm3d.json', 'burst.json'))
    for name, meta, key in (('crops.npz', crops, 'crops_sha256'), ('network.npz', net, 'network_sha256'),
                            ('bm3d.npz', bm3d_meta, 'bm3d_sha256'), ('burst.npz', burst_meta, 'burst_sha256')):
        if digest(evaluation/name) != meta[key]:
            raise ValueError(name+' changed')
    arrays, networks, bm3ds, bursts = (np.load(evaluation/n) for n in ('crops.npz', 'network.npz', 'bm3d.npz', 'burst.npz'))
    output.mkdir(exist_ok=False)
    inner = (slice(None), slice(BORDER, -BORDER), slice(BORDER, -BORDER))
    rows = []
    for entry in crops['entries']:
        key, count = entry['key'], burst_meta['counts'][entry['key']]
        valid = arrays[key+'-valid']
        ratios = {}
        for index, frame in enumerate(arrays[key+'-noisy'].astype(np.float64)):
            others = (bursts[key+'-sum']-frame)/(count-1)
            mapping = fit_mapping(frame, others, valid)
            g = np.array([m[0] for m in mapping])[:, None, None]
            o = np.array([m[1] for m in mapping])[:, None, None]
            noise = band_power(((g*frame+o)-others)[inner])
            methods = [('vst_bm3d', bm3ds[key][index])]+[(f'network_seed{s}', networks[f'{key}-seed{s}'][index]) for s in net['seeds']]
            for name, estimate in methods:
                error = band_power(((g*estimate.astype(np.float64)+o)-others)[inner])
                ratios.setdefault(name, []).append([e/max(n, 1e-30) for e, n in zip(error, noise)])
        rows.append(dict(key=key, scene=entry['scene'], iso=entry['iso'], burst_frames=count, reference_floor_ratio=1/(count-1),
                         median_band_ratio={k: np.median(v, axis=0).tolist() for k, v in ratios.items()}))
    result = dict(schema_version=1, status='residual_spectrum_diagnosis', measured_at_utc=datetime.now(timezone.utc).isoformat(),
        evaluation=str(evaluation.resolve()), bands_cycles_per_packed_cell=[list(b) for b in BANDS], rows=rows,
        tool_sha256=digest(Path(__file__)),
        limits=['Same-burst reference keeps fixed-pattern noise and its own noise (floor near 1/(N-1) of the noise power)',
                'Three frames per burst, one central crop; median over frames'])
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evaluation', type=Path, default=BUILD/'real-noise-evaluation-v003')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.evaluation.resolve(), args.output.resolve())
