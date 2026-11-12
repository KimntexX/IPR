"""Audit the paired real-noise scenes of 1 November before using them for evaluation.

For each scene: stability of the ISO100 reference burst (split halves against
the temporal noise), light drift inside each burst, translation between the
ISO100 mean and each high-ISO mean, and the per-plane gain/offset that maps a
high-ISO mean onto the reference. Read only; the reference means are cached
in the build folder for the later evaluation.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path

from prepare_next_training import digest, write


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
# The card's 101EOSR6 folder, moved by the user into the colorimetry protocol images on 10 November.
MOVED_CARD = ROOT.parent.parent/'tests'/'image'/'colorimétrie'/'protocole'/'101EOSR6'
CARD = MOVED_CARD if MOVED_CARD.is_dir() else Path('D:/DCIM/101EOSR6')
SCENES = {
    'interior_day': {100: (134, 151), 3200: (152, 180), 12800: (181, 194), 51200: (195, 209)},
    'leaves_grass': {100: (211, 243), 1600: (244, 262), 6400: (263, 280), 12800: (281, 300)},
    'interior_night': {100: (320, 327), 1600: (310, 319), 3200: (328, 339), 6400: (340, 351), 12800: (352, 366)},
}
BLACK = {100: 512.}
WHITE = {100: 13995.}
DEFAULT_BLACK, DEFAULT_WHITE = 2048., 14351.
POSITIONS = ((0, 0), (0, 1), (1, 0), (1, 1))


def files(first, last, folder=CARD):
    return [folder/f'6V1A{n:04d}.CR3' for n in range(first, last+1)]


def packed_normalized(mosaic, iso):
    import numpy as np
    black, white = BLACK.get(iso, DEFAULT_BLACK), WHITE.get(iso, DEFAULT_WHITE)
    # The visible R6 III mosaic has an odd row count: keep whole 2x2 cells only.
    mosaic = mosaic[:mosaic.shape[0]//2*2, :mosaic.shape[1]//2*2]
    return np.stack([(mosaic[y::2, x::2].astype(np.float32)-black)/(white-black) for y, x in POSITIONS])


def split_half_ratio(frames_sum_a, count_a, frames_sum_b, count_b, temporal_variance):
    """Observed variance of the half-mean difference over the variance expected from temporal noise alone."""
    import numpy as np
    difference = frames_sum_a/count_a-frames_sum_b/count_b
    per_plane = np.asarray(temporal_variance, dtype=np.float64).reshape(difference.shape[0], -1).mean(axis=1)
    expected = per_plane*(1/count_a+1/count_b)
    return float(np.median(difference.var(axis=(-2, -1))/np.maximum(expected, 1e-30)))


def phase_shift(reference, moving):
    """Integer translation (dy, dx) and peak sharpness from phase correlation."""
    import numpy as np
    a = reference-reference.mean()
    b = moving-moving.mean()
    window = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    cross = np.fft.fft2(a*window)*np.conj(np.fft.fft2(b*window))
    surface = np.fft.ifft2(cross/np.maximum(np.abs(cross), 1e-12)).real
    peak = np.unravel_index(np.argmax(surface), surface.shape)
    shift = [int(p if p <= n//2 else p-n) for p, n in zip(peak, surface.shape)]
    return shift, float(surface.max()/max(surface.std(), 1e-12))


def gain_offset(reference, moving):
    """Least-squares moving*gain+offset = reference, one pair per plane."""
    import numpy as np
    rows = []
    for plane in range(reference.shape[0]):
        x, y = moving[plane].ravel().astype(np.float64), reference[plane].ravel().astype(np.float64)
        design = np.stack([x, np.ones_like(x)], axis=1)
        gain, offset = np.linalg.lstsq(design, y, rcond=None)[0]
        rows.append(dict(gain=float(gain), offset=float(offset)))
    return rows


def burst_statistics(paths, iso):
    """Running sums, per-frame plane means, and temporal variance from consecutive differences."""
    import numpy as np
    import rawpy
    total, half, means, diff_var, previous = None, None, [], None, None
    middle = len(paths)//2
    for index, path in enumerate(paths):
        with rawpy.imread(str(path)) as raw:
            if raw.raw_colors_visible[:2, :2].tolist() != [[0, 1], [3, 2]]:
                raise ValueError('RGGB R6 III phase required')
            value = packed_normalized(raw.raw_image_visible, iso).astype(np.float64)
        means.append(value.mean(axis=(-2, -1)).tolist())
        total = value.copy() if total is None else total+value
        if index == middle-1:
            half = total.copy()
        if previous is not None:
            step = (value-previous).var(axis=(-2, -1))/2
            diff_var = step if diff_var is None else diff_var+step
        previous = value
    temporal = diff_var/(len(paths)-1)
    return total, half, means, temporal


def run(output):
    import numpy as np
    output.mkdir(exist_ok=False)
    result = dict(schema_version=1, measured_at_utc=datetime.now(timezone.utc).isoformat(),
        status='audit_of_paired_real_noise_scenes', scenes={}, tool_sha256=digest(Path(__file__)),
        normalization=dict(black=dict(iso100=BLACK[100], other=DEFAULT_BLACK), white=dict(iso100=WHITE[100], other=DEFAULT_WHITE)),
        limits=['Card files read in place; capture order and protocol stated by the user (tripod, phone release)',
                'Split-half ratio near 1 means the reference burst is stable within its own temporal noise',
                'Phase correlation on the mean green plane, integer packed cells; subcell motion not measured',
                'Gain/offset fit over whole planes; it corrects exposure and light drift, not local motion'])
    for scene, bursts in SCENES.items():
        entry = dict(bursts={})
        reference_total = None
        for iso, (first, last) in bursts.items():
            paths = files(first, last)
            missing = [p.name for p in paths if not p.exists()]
            if missing:
                raise FileNotFoundError(f'{scene} ISO{iso}: missing {missing}')
            total, half, means, temporal = burst_statistics(paths, iso)
            count = len(paths)
            mean = (total/count).astype(np.float32)
            plane_means = np.array(means)
            drift = (plane_means[-1]-plane_means[0])/np.maximum(plane_means.mean(axis=0), 1e-12)
            burst = dict(first=paths[0].name, last=paths[-1].name, frames=count,
                         light_drift_first_to_last=[float(v) for v in drift],
                         split_half_ratio=split_half_ratio(half, count//2, total-half, count-count//2, temporal),
                         temporal_sigma_by_plane=np.sqrt(temporal).tolist())
            if iso == 100:
                reference_total = mean
                np.save(output/f'{scene}-iso100-mean.npy', mean, allow_pickle=False)
                burst['reference_mean_sha256'] = digest(output/f'{scene}-iso100-mean.npy')
            else:
                green = slice(1, 3)
                ref_g = reference_total[green].mean(axis=0)
                mov_g = mean[green].mean(axis=0)
                h, w = ref_g.shape
                crop = (slice(h//2-512, h//2+512), slice(w//2-768, w//2+768))
                shift, sharpness = phase_shift(ref_g[crop], mov_g[crop])
                burst.update(shift_packed_cells=shift, correlation_peak_sharpness=sharpness,
                             gain_offset_to_reference=gain_offset(reference_total[:, crop[0], crop[1]], mean[:, crop[0], crop[1]]))
            entry['bursts'][str(iso)] = burst
            write(output/'progress.json', result | {'current': f'{scene} ISO{iso}'})
        result['scenes'][scene] = entry
    write(output/'result.json', result)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (ROOT/'docs/validation/raw'/f'neural-{output.name}-{stamp}.json').open('x', encoding='utf-8') as stream:
        json.dump(result, stream, allow_nan=False, separators=(',', ':'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    run(parser.parse_args().output.resolve())
