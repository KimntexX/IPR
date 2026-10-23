"""Isolated five-window luminance experiment; never modifies production or photos."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import time

import numpy as np
import scipy
from scipy.ndimage import gaussian_filter, median_filter
from numpy.lib.stride_tricks import sliding_window_view

WINDOWS = ((0, 0), (2, 0), (2, 2), (0, 2), (1, 1))  # NW, SW, SE, NE, centre


def five_windows(plane, sigma, gated=True):
    """Input/output are linear scalar luminance, no clipping or in-place update.

    sigma is supplied by the synthetic experiment, not estimated from a camera.
    Quadrants are 3x3 windows sharing the tested pixel inside a 5x5 footprint.
    """
    plane = np.asarray(plane, dtype=np.float64)
    if plane.ndim != 2 or min(plane.shape) < 5 or not np.isfinite(plane).all():
        raise ValueError("Expected a finite luminance plane of at least 5x5")
    if not np.isfinite(sigma) or sigma < 0:
        raise ValueError("sigma must be finite and nonnegative")
    patch = sliding_window_view(np.pad(plane, 2, mode="reflect"), (5, 5))
    medians, dispersions = [], []
    for y, x in WINDOWS:
        block = patch[..., y:y + 3, x:x + 3]
        med = np.median(block, axis=(-2, -1))
        medians.append(med)
        dispersions.append(np.median(np.abs(block - med[..., None, None]), axis=(-2, -1)))
    medians, dispersions = np.stack(medians), np.stack(dispersions)
    centre = patch[..., 1:4, 1:4].reshape(*plane.shape, 9)
    neighbours = np.delete(centre, 4, axis=-1)
    residual = plane - np.median(neighbours, axis=-1)
    direction = np.sign(residual)  # positive = brighter than local surroundings
    agreement = np.mean((plane[..., None] - neighbours) * direction[..., None] > 0, axis=-1)
    # Median of all five estimates: symmetric across the four corners.
    candidate = np.median(medians, axis=0)
    if not gated:
        return candidate, direction, np.ones(plane.shape, dtype=bool)
    # Symmetric tie handling avoids privileging the NW window on equal MADs.
    least = np.min(dispersions, axis=0)
    eligible = np.isclose(dispersions, least[None], rtol=0, atol=1e-12)
    candidate = np.nanmedian(np.where(eligible, medians, np.nan), axis=0)
    mask = (np.abs(residual) > 2.5 * sigma) & (agreement >= .75)
    return np.where(mask, candidate, plane), direction, mask


def scenes(size=128):
    y, x = np.mgrid[:size, :size]
    return {
        "flat_dark": np.full((size, size), .04),
        "ramp": .03 + .9 * x / (size - 1),
        "vertical_edge": np.where(x < size // 2, .1, .8),
        "diagonal_edge": np.where(x < y, .1, .8),
        "fine_texture": .4 + .12 * np.sin(x * np.pi / 2) * np.sin(y * np.pi / 3),
        "thin_lines": np.where((x % 16 == 0) | (y % 23 == 0), .8, .1),
        "hdr_ramp": -.03 + 2.0 * x / (size - 1),
    }


def run():
    rows = []
    for seed in (31, 47, 83):
        rng = np.random.default_rng(seed)
        for scene, clean in scenes().items():
            for noise in ("gaussian", "impulse", "signal_dependent"):
                sigma = .025
                impulse = rng.random(clean.shape) < .03
                if noise == "gaussian":
                    noisy = clean + rng.normal(0, sigma, clean.shape)
                elif noise == "impulse":
                    noisy = np.where(impulse, rng.choice([-.1, 1.1], clean.shape), clean)
                else:
                    noisy = clean + rng.normal(0, np.sqrt(np.maximum(clean, 0) / 2500 + .01 ** 2))
                for method in ("unchanged", "median3", "gaussian08", "five_direct", "five_gated"):
                    start = time.perf_counter()
                    direction = mask = None
                    if method.startswith("five"):
                        result, direction, mask = five_windows(noisy, sigma, method == "five_gated")
                    elif method == "median3":
                        result = median_filter(noisy, size=3, mode="mirror")
                    elif method == "gaussian08":
                        result = gaussian_filter(noisy, sigma=.8, mode="mirror")
                    else:
                        result = noisy.copy()
                    duration = time.perf_counter() - start
                    region = np.s_[4:-4, 4:-4]
                    error = result[region] - clean[region]
                    gradient_mask = np.hypot(*np.gradient(clean))[region] > .04
                    row = dict(scene=scene, noise=noise, seed=seed, method=method,
                               mae=float(np.mean(np.abs(error))), rmse=float(np.sqrt(np.mean(error ** 2))),
                               mean_bias=float(np.mean(error)), seconds=duration,
                               measured_pixels=int(error.size),
                               structure_mae=float(np.mean(np.abs(error[gradient_mask]))) if gradient_mask.any() else None,
                               flat_rmse=float(np.sqrt(np.mean(error[~gradient_mask] ** 2))) if (~gradient_mask).any() else None)
                    if direction is not None:
                        actual = np.sign(noisy[region] - clean[region])
                        changed = actual != 0
                        row["polarity_accuracy"] = float(np.mean(direction[region][changed] == actual[changed]))
                        row["polarity_denominator"] = int(changed.sum())
                        row["modified_fraction"] = float(mask[region].mean())
                        if noise == "impulse":
                            row["impulse_recall"] = float(mask[region][impulse[region]].mean())
                            row["clean_false_positive_rate"] = float(mask[region][~impulse[region]].mean())
                            row["impulse_pixels"] = int(impulse[region].sum())
                            row["clean_pixels"] = int((~impulse[region]).sum())
                            row["impulse_detected"] = int((mask[region] & impulse[region]).sum())
                            row["clean_modified"] = int((mask[region] & ~impulse[region]).sum())
                    rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    result = dict(campaign_id="directional-noise-v002-" + now.strftime("%Y%m%dT%H%M%SZ"),
                  schema_version=1, data_version=2, measured_at_utc=now.isoformat(),
                  supersedes_campaign_id="directional-noise-v001-20261020T142535Z",
                  revision_reason="Preserve explicit impulse detection numerators and denominators",
                  revision=revision, analyzer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  protocol="protocole-bruit.md#étude-exploratoire--cinq-médianes-directionnelles",
                  configuration=dict(size=[128, 128], seeds=[31, 47, 83], crop_border=4,
                                     window_offsets_yx=WINDOWS, window_size=3, noise_sigma=.025,
                                     gate_sigma=2.5, consensus=.75, impulse_probability=.03,
                                     poisson_gaussian_variance="max(Y,0)/2500 + 0.01^2; Gaussian approximation",
                                     luminance_space="synthetic linear scalar; no RGB or clipping",
                                     timing="single run, Python CPU, diagnostic only"),
                  audit=dict(production_modified=False, real_photographs_measured=False,
                             lightroom_compared=False, sigma_estimator_validated=False), rows=run())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(f"{len(result['rows'])} measurements: {args.output}")


if __name__ == "__main__":
    main()
