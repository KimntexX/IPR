"""Nine-neighbourhood luminance study; convex model fusion, no production writes."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import numpy as np
import scipy
from scipy.ndimage import gaussian_filter, maximum_filter
from scipy.optimize import minimize
from numpy.lib.stride_tricks import sliding_window_view

MODELS = ("unchanged", "nine_median", "precision_mean", "huber", "axis_preserving")
NEIGHBOURHOODS = ("NW", "SW", "SE", "NE", "centre", "above", "below", "left", "right")


def observations(plane):
    plane = np.asarray(plane, dtype=np.float64)
    if plane.ndim != 2 or min(plane.shape) < 7 or not np.isfinite(plane).all():
        raise ValueError("Expected a finite luminance plane of at least 7x7")
    patch = sliding_window_view(np.pad(plane, 3, mode="reflect"), (7, 7))
    locations = []
    for y, x in ((1, 1), (3, 1), (3, 3), (1, 3), (2, 2)):
        locations.append([(i, j) for i in range(y, y + 3) for j in range(x, x + 3)
                          if (i, j) != (3, 3)])
    locations += [[(3 - i, 3) for i in (1, 2, 3)], [(3 + i, 3) for i in (1, 2, 3)],
                  [(3, 3 - i) for i in (1, 2, 3)], [(3, 3 + i) for i in (1, 2, 3)]]
    medians, spreads = [], []
    for support in locations:
        samples = np.stack([patch[..., y, x] for y, x in support])
        med = np.median(samples, axis=0)
        medians.append(med)
        spreads.append(1.4826 * np.median(np.abs(samples - med[None]), axis=0))
    return np.stack(medians), np.stack(spreads)


def noise_scale(plane):
    a = plane[:plane.shape[0] // 2 * 2, :plane.shape[1] // 2 * 2]
    hh = (a[::2, ::2] - a[1::2, ::2] - a[::2, 1::2] + a[1::2, 1::2]) / 2
    return float(np.median(np.abs(hh - np.median(hh))) / .67448975)


def candidates(plane):
    plane = np.asarray(plane, dtype=np.float64)
    med, spread = observations(plane)
    sigma = noise_scale(plane)
    scale = max(sigma, 1e-8)
    middle = np.median(med, axis=0)
    # Diagonal-precision comparator, NOT a covariance-aware GLS estimator.
    precision = np.asarray([8] * 5 + [3] * 4)[:, None, None] / (spread ** 2 + scale ** 2)
    mean = np.sum(precision * med, axis=0) / np.sum(precision, axis=0)
    robust = middle.copy()
    local_scale = np.maximum(1.4826 * np.median(np.abs(med - middle), axis=0), scale)
    for _ in range(8):
        weights = np.minimum(1, 1.5 * local_scale / np.maximum(np.abs(med - robust), 1e-12))
        robust = np.sum(weights * med, axis=0) / np.sum(weights, axis=0)
    pairs = ((0, 2), (1, 3), (5, 6), (7, 8))
    values = np.stack([.5 * (med[a] + med[b]) for a, b in pairs])
    scores = np.stack([np.abs(med[a] - med[b]) + np.abs(values[i] - plane)
                       for i, (a, b) in enumerate(pairs)])
    weights = np.exp(-(scores - np.min(scores, axis=0)) / scale)
    axis = np.sum(weights * values, axis=0) / np.sum(weights, axis=0)
    return np.stack((plane.copy(), middle, mean, robust, axis)), sigma


def fit_weights(error_second_moment):
    q = np.asarray(error_second_moment, dtype=np.float64)
    if q.ndim != 2 or q.shape[0] != q.shape[1] or not q.size or not np.isfinite(q).all():
        raise ValueError("Expected a finite square error second-moment matrix")
    if not np.allclose(q, q.T) or np.linalg.eigvalsh(q).min() < -1e-10:
        raise ValueError("Error second moment must be symmetric positive semidefinite")
    normalized = q / max(float(np.trace(q)), 1e-15)
    count = q.shape[0]
    result = minimize(lambda w: float(w @ normalized @ w), np.full(count, 1 / count),
                      jac=lambda w: 2 * normalized @ w, method="SLSQP",
                      bounds=[(0, 1)] * count,
                      constraints={"type": "eq", "fun": lambda w: w.sum() - 1,
                                   "jac": lambda w: np.ones(count)},
                      options={"ftol": 1e-12, "maxiter": 1000})
    if not result.success:
        raise RuntimeError(result.message)
    weights = np.maximum(result.x, 0)
    return weights / weights.sum()


def scene_set(split, size=96):
    y, x = np.mgrid[:size, :size]
    if split == "calibration":
        return {"flat_dark": np.full((size, size), .04), "ramp": .03 + .9 * x / (size - 1),
                "vertical_edge": np.where(x < size // 2, .1, .8),
                "fine_texture": .4 + .12 * np.sin(x * np.pi / 2) * np.sin(y * np.pi / 3),
                "thin_grid": np.where((x % 16 == 0) | (y % 23 == 0), .8, .1)}
    if split != "validation":
        raise ValueError("Unknown split")
    return {"flat_light": np.full((size, size), .6), "curved_ramp": .1 + .8 * (x / (size - 1)) ** 2,
            "diagonal_edge": np.where(x < y + 3, .12, .76),
            "rotated_texture": .4 + .12 * np.sin((x + y) * np.pi / 2.7) * np.sin((x - y) * np.pi / 4.1),
            "diagonal_lines": np.where(((x - y) % 17 == 0) | ((x + y) % 25 == 0), .8, .1),
            "signed_hdr_ramp": -.03 + 2 * x / (size - 1)}


def cases(split):
    seeds = (101, 107) if split == "calibration" else (211, 223)
    for seed in seeds:
        rng = np.random.default_rng(seed)
        for scene, clean in scene_set(split).items():
            for noise in ("gaussian", "correlated_gaussian", "impulse", "signal_dependent"):
                contaminated = rng.random(clean.shape) < .03
                if noise == "impulse":
                    noisy = np.where(contaminated, rng.choice([-.1, 1.1], clean.shape), clean)
                elif noise == "signal_dependent":
                    noisy = clean + rng.normal(0, np.sqrt(np.maximum(clean, 0) / 2500 + .01 ** 2))
                else:
                    error = rng.normal(0, .025, clean.shape)
                    if noise == "correlated_gaussian":
                        error = gaussian_filter(error, .65, mode="mirror")
                        error *= .025 / np.std(error)
                    noisy = clean + error
                predictions, sigma = candidates(noisy)
                yield dict(split=split, scene=scene, noise=noise, seed=seed, clean=clean,
                           noisy=noisy, predictions=predictions, estimated_sigma=sigma)


def run():
    crop = np.s_[6:-6, 6:-6]
    calibration = list(cases("calibration"))
    validation = list(cases("validation"))
    q = np.zeros((len(MODELS), len(MODELS)))
    count = 0
    for case in calibration:
        errors = (case["predictions"] - case["clean"])[(slice(None),) + crop].reshape(len(MODELS), -1)
        q += errors @ errors.T
        count += errors.shape[1]
    q /= count
    fitted = fit_weights(q)
    rows = []
    for case in calibration + validation:
        outputs = list(case["predictions"])
        outputs += [np.mean(case["predictions"], axis=0), np.einsum("i,ihw->hw", fitted, case["predictions"])]
        structure = maximum_filter(np.hypot(*np.gradient(case["clean"])), size=3)[crop] > .04
        for method, output in zip(MODELS + ("ensemble_equal", "ensemble_fitted"), outputs):
            errors = output[crop] - case["clean"][crop]
            rows.append(dict(split=case["split"], scene=case["scene"], noise=case["noise"], seed=case["seed"],
                             method=method, estimated_sigma=case["estimated_sigma"],
                             measured_pixels=int(errors.size), structure_pixels=int(structure.sum()),
                             mae=float(np.mean(np.abs(errors))), rmse=float(np.sqrt(np.mean(errors ** 2))),
                             mean_bias=float(np.mean(errors)),
                             structure_mae=float(np.mean(np.abs(errors[structure]))) if structure.any() else None,
                             flat_rmse=float(np.sqrt(np.mean(errors[~structure] ** 2))) if (~structure).any() else None))
    return dict(weights=dict(zip(MODELS, fitted.tolist())),
                calibration_error_second_moment=q.tolist(), calibration_pixels=count,
                calibration_objective=float(fitted @ q @ fitted), rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    result = dict(campaign_id="luminance-ensemble-v001-" + now.strftime("%Y%m%dT%H%M%SZ"),
                  schema_version=1, data_version=1, measured_at_utc=now.isoformat(),
                  revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                  analyzer_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  protocol="protocole-bruit.md#étude-luminance--neuf-voisinages-et-fusion-destimateurs",
                  configuration=dict(size=[96, 96], crop_border=6, neighbourhoods=NEIGHBOURHOODS,
                                     centre_excluded=True, directional_radius=3, mirror_padding=True,
                                     calibration_scenes=list(scene_set("calibration")), calibration_seeds=[101, 107],
                                     validation_scenes=list(scene_set("validation")), validation_seeds=[211, 223],
                                     scalar_space="linear luminance; signed HDR, no clipping",
                                     noise="Gaussian sigma .025; correlated Gaussian blur .65 rescaled to .025; "
                                           "3% impulses at -.1/1.1; heteroscedastic Gaussian variance max(Y,0)/2500+.01^2",
                                     estimated_sigma="diagonal Haar MAD / .67448975, one scalar per image",
                                     huber_iterations=8, huber_factor=1.5, numerical_scale_floor=1e-8,
                                     fusion="global nonnegative weights summing to 1; calibration MSE QP; no validation tuning"),
                  audit=dict(production_modified=False, real_images_measured=False, posterior_calibrated=False,
                             noise_detector_compared=False, covariance_of_observations_calibrated=False,
                             production_denoiser_compared=False), **run())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(f"{len(result['rows'])} measurements: {args.output}")
    print("Calibration weights:", result["weights"])


if __name__ == "__main__":
    main()
