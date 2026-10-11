"""Shared linear-light metrics with explicit masks, units and denominators."""
import numpy as np

from .native import linearize

WEIGHTS = np.array([.2126, .7152, .0722])


def mean(values, mask=None):
    selected = values if mask is None else values[mask]
    return float(np.mean(selected)) if selected.size else None


def ratio(numerator, denominator):
    return float(numerator / denominator) if denominator is not None and denominator > 1e-12 else None


def hsv(rgb):
    maximum, minimum = rgb.max(axis=-1), rgb.min(axis=-1)
    chroma = maximum - minimum
    safe = np.maximum(chroma, 1e-12)
    r, g, b = (rgb[..., i] for i in range(3))
    hue = np.where(maximum == r, ((g - b) / safe) % 6,
                   np.where(maximum == g, (b - r) / safe + 2, (r - g) / safe + 4)) * 60
    return np.where(chroma > 1e-12, hue, 0), chroma / np.maximum(maximum, 1e-12), maximum


def circular_delta(a, b):
    return (a - b + 180) % 360 - 180


def circular_mean(angles, mask):
    if not mask.any():
        return None
    z = np.exp(1j * np.deg2rad(angles[mask])).mean()
    return float(np.rad2deg(np.angle(z))) if abs(z) > 1e-10 else None


def common(reference, variant):
    if reference.shape != variant.shape:
        raise ValueError("Within-engine geometry mismatch; no implicit resize/crop")
    base, changed = linearize(reference), linearize(variant)
    y0, y1 = base @ WEIGHTS, changed @ WEIGHTS
    clip0 = (reference >= 1 - 1 / 65535).any(axis=-1)
    clip1 = (variant >= 1 - 1 / 65535).any(axis=-1)
    result = {"samples": int(y0.size), "mean_Y_neutral": mean(y0), "mean_Y_variant": mean(y1),
              "Y_ratio": ratio(y1.sum(), y0.sum()), "mean_Y_delta": mean(y1 - y0),
              "MAE_linear_to_own_reference": mean(np.abs(changed - base)),
              "identical_on_samples": bool(np.array_equal(reference, variant)),
              "clipped_samples_neutral": int(clip0.sum()), "clipped_samples_variant": int(clip1.sum()),
              "newly_clipped_samples": int((clip1 & ~clip0).sum()),
              "clipped_percent_variant": 100 * mean(clip1),
              "black_samples_variant": int((y1 <= .001).sum()),
              "channel_mean_RGB_neutral": base.mean(axis=(0, 1)).tolist(),
              "channel_mean_RGB_variant": changed.mean(axis=(0, 1)).tolist(),
              "quantile_Y_neutral": np.quantile(y0, [.05, .5, .95]).tolist(),
              "quantile_Y_variant": np.quantile(y1, [.05, .5, .95]).tolist()}
    return result, base, changed, y0, y1


def zones(y0, y1):
    bounds = np.quantile(y0, [.2, .4, .6, .8])
    masks = {"bottom20": y0 <= bounds[0], "middle40_60": (y0 >= bounds[1]) & (y0 <= bounds[2]),
             "top20": y0 >= bounds[3]}
    return {name: {"samples": int(mask.sum()), "mean_Y_delta": mean(y1 - y0, mask),
                   "Y_ratio": ratio(y1[mask].sum(), y0[mask].sum()),
                   "neutral_quantile_bounds": bounds.tolist(), "ties_included": True}
            for name, mask in masks.items()}


def chroma_groups(base, changed):
    h0, c0, _ = hsv(base)
    h1, c1, _ = hsv(changed)
    valid = (base.min(axis=-1) > 1e-5) & (base.max(axis=-1) < .98) & (changed.max(axis=-1) < .98)
    groups = {"low_chroma": valid & (c0 >= .02) & (c0 < .25), "high_chroma": valid & (c0 >= .5)}
    return {name: {"samples": int(mask.sum()), "mean_chroma_delta": mean(c1 - c0, mask),
                   "mean_relative_chroma_delta": mean((c1 - c0) / np.maximum(c0, 1e-12), mask),
                   "hue_delta_degrees": circular_mean(circular_delta(h1, h0), mask & (c1 >= .02))}
            for name, mask in groups.items()}


def blur(plane, kernel=(1, 4, 6, 4, 1)):
    radius, scale = len(kernel) // 2, sum(kernel)
    p = np.pad(plane, ((radius, radius), (0, 0)), mode="edge")
    vertical = sum(k * p[i:i + plane.shape[0]] for i, k in enumerate(kernel)) / scale
    p = np.pad(vertical, ((0, 0), (radius, radius)), mode="edge")
    return sum(k * p[:, i:i + plane.shape[1]] for i, k in enumerate(kernel)) / scale


def haar_sigma(plane):
    height, width = (plane.shape[0] // 2) * 2, (plane.shape[1] // 2) * 2
    if min(height, width) < 2:
        return None
    p = plane[:height, :width]
    hh = (p[::2, ::2] - p[::2, 1::2] - p[1::2, ::2] + p[1::2, 1::2]) / 2
    return float(np.median(np.abs(hh - np.median(hh))) / .6744897501960817)


def detail(rgb):
    y = rgb @ WEIGHTS
    if min(y.shape) < 2:
        raise ValueError("Spatial ROI must be at least 2x2")
    co = rgb[..., 0] - rgb[..., 2]
    cg = rgb[..., 1] - (rgb[..., 0] + rgb[..., 2]) / 2
    dy, dx = np.gradient(y)
    sigma_co, sigma_cg = haar_sigma(co), haar_sigma(cg)
    smooth = y
    for _ in range(4):
        smooth = blur(smooth)
    return {"sigma_Y_Haar": haar_sigma(y),
            "sigma_chroma_Haar": float(np.hypot(sigma_co, sigma_cg) / np.sqrt(2)),
            "gradient_RMS": float(np.sqrt(np.mean(dx * dx + dy * dy))),
            "micro_residual_RMS": float(np.sqrt(np.mean((y - blur(y, (1, 2, 1))) ** 2))),
            "local_residual_RMS": float(np.sqrt(np.mean((y - smooth) ** 2))),
            "mean_Co": mean(co), "mean_Cg": mean(cg)}
