"""Single-RAW Poisson-Gauss law per CFA position (raster order), exploratory.

b: robust variance of the masked left columns (column pattern removed);
a: slope from the smoothest blocks of a same-plane residual, b fixed, lower
quantile corrected by its expectation for pure Gaussian noise.
"""
import numpy as np
import rawpy

BLOCK = 16
QUANTILE = 0.1
POSITIONS = ((0, 0), (0, 1), (1, 0), (1, 1))
_bias = {}


def quantile_bias(q):
    if q not in _bias:
        rng = np.random.default_rng(1)
        x = rng.standard_normal((4000, BLOCK + 2, BLOCK + 2))
        r = x[:, 1:-1, 1:-1] - 0.25 * (x[:, :-2, 1:-1] + x[:, 2:, 1:-1] + x[:, 1:-1, :-2] + x[:, 1:-1, 2:])
        v = (np.median(np.abs(r - np.median(r, axis=(1, 2), keepdims=True)), axis=(1, 2)) / 0.6745) ** 2 / 1.25
        _bias[q] = float(np.quantile(v, q))
    return _bias[q]


def blocks(plane):
    r = plane[1:-1, 1:-1] - 0.25 * (plane[:-2, 1:-1] + plane[2:, 1:-1] + plane[1:-1, :-2] + plane[1:-1, 2:])
    h, w = (r.shape[0] // BLOCK) * BLOCK, (r.shape[1] // BLOCK) * BLOCK
    cut = lambda x: x[:h, :w].reshape(h // BLOCK, BLOCK, w // BLOCK, BLOCK).swapaxes(1, 2).reshape(-1, BLOCK * BLOCK)
    rb, mb = cut(r), cut(plane[1:1 + h, 1:1 + w])
    v = (np.median(np.abs(rb - np.median(rb, axis=1, keepdims=True)), axis=1) / 0.6745) ** 2 / 1.25
    return mb.mean(axis=1), mb.max(axis=1), v


def estimate_law(path):
    """[(shot_slope_dn, read_variance_dn2)] for the four raster positions."""
    with rawpy.imread(str(path)) as raw:
        full = raw.raw_image.astype(np.float64)
        visible = raw.raw_image_visible.astype(np.float64)
        top, left, white = raw.sizes.top_margin, raw.sizes.left_margin, float(raw.white_level)
    law = []
    for dy, dx in POSITIONS:
        masked = full[top + dy:top + raw_height(visible):2, 8 + dx:left - 8:2]
        black = float(np.median(masked))
        cols = masked - np.median(masked, axis=0, keepdims=True)
        b = float((np.median(np.abs(cols - np.median(cols))) / 0.6745) ** 2)
        mean, peak, var = blocks(visible[dy::2, dx::2] - black)
        keep = (peak < 0.95 * (white - black)) & (mean > 0)
        edges = np.unique(np.quantile(mean[keep], np.linspace(0, 1, 25)))
        estimates = []
        for lo, hi in zip(edges[:-1], edges[1:]):
            sel = keep & (mean >= lo) & (mean < hi)
            if sel.sum() < 40:
                continue
            m = float(np.median(mean[sel]))
            estimates.append((float(np.quantile(var[sel], QUANTILE)) / quantile_bias(QUANTILE) - b) / m)
        estimates = np.sort(estimates)
        a = float(np.mean(estimates[:max(1, len(estimates) // 2)]))   # texture only adds: lower half
        law.append((a, b))
    return law


def raw_height(visible):
    return visible.shape[0]
