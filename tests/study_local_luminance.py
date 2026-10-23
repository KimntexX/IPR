"""Local convex fusion and abstention study; synthetic luminance, not production."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess

import numpy as np
import scipy
from scipy.ndimage import gaussian_filter, median_filter, sobel, maximum_filter
from numpy.lib.stride_tricks import sliding_window_view
from study_luminance_ensemble import candidates, cases, fit_weights, noise_scale

MODELS = ("unchanged", "nine_median", "precision_mean", "huber", "axis_preserving", "linear_axes")
REGIMES = ("smooth", "coherent_edge", "supported_line", "uncertain")


def local_features(plane):
    """Eight distinct half-lines, including diagonals; no target in their supports."""
    plane = np.asarray(plane, dtype=np.float64)
    base, sigma = candidates(plane)  # validates input; five original candidates
    scale = max(sigma, 1e-8)
    patch = sliding_window_view(np.pad(plane, 3, mode="reflect"), (7, 7))
    medians, spreads = [], []
    for dy, dx in ((1, 0), (0, 1), (1, 1), (1, -1)):
        for sign in (-1, 1):
            sample = np.stack([patch[..., 3 + sign * step * dy, 3 + sign * step * dx]
                               for step in (1, 2, 3)])
            med = np.median(sample, axis=0)
            medians.append(med)
            spreads.append(1.4826 * np.median(np.abs(sample - med), axis=0))
    medians, spreads = np.stack(medians), np.stack(spreads)
    estimates = .5 * (medians[::2] + medians[1::2])
    disagreement = np.abs(medians[::2] - medians[1::2])
    scores = disagreement + .5 * (spreads[::2] + spreads[1::2]) + np.abs(estimates - plane)
    ray_weights = np.exp(-(scores - np.min(scores, axis=0)) / scale)
    directional = np.sum(ray_weights * estimates, axis=0) / np.sum(ray_weights, axis=0)
    support = ((np.abs(estimates - plane) <= 2 * scale) & (disagreement <= 2 * scale)
               & (np.maximum(spreads[::2], spreads[1::2]) <= 2 * scale)).any(axis=0)
    guide = gaussian_filter(plane, .8, mode="mirror")
    gx = sobel(guide, axis=1, mode="mirror") / 8
    gy = sobel(guide, axis=0, mode="mirror") / 8
    xx = gaussian_filter(gx * gx, 1, mode="mirror")
    yy = gaussian_filter(gy * gy, 1, mode="mirror")
    xy = gaussian_filter(gx * gy, 1, mode="mirror")
    energy = np.sqrt(xx + yy)
    coherence = np.sqrt((xx - yy) ** 2 + 4 * xy ** 2) / np.maximum(xx + yy, 1e-16)
    structured = energy > .35 * scale
    line = support & (np.abs(plane - median_filter(plane, 3, mode="mirror")) > 3 * scale)
    regime = np.full(plane.shape, 3, dtype=np.int8)
    regime[~structured] = 0
    regime[structured & (coherence > .7)] = 1
    regime[line] = 2
    return np.concatenate((base, directional[None])), regime, sigma


def calibrate(records):
    sums = np.zeros((len(REGIMES), len(MODELS), len(MODELS)))
    counts = np.zeros(len(REGIMES), dtype=np.int64)
    crop = np.s_[6:-6, 6:-6]
    for case in records:
        outputs, regime, _ = local_features(case["noisy"])
        error = (outputs - case["clean"])[(slice(None),) + crop].reshape(len(MODELS), -1)
        labels = regime[crop].ravel()
        for index in range(len(REGIMES)):
            selected = error[:, labels == index]
            sums[index] += selected @ selected.T
            counts[index] += selected.shape[1]
    global_weights = fit_weights(sums.sum(axis=0) / counts.sum())
    weights = []
    for index in range(len(REGIMES)):
        if counts[index] < 1024:
            weights.append(np.eye(len(MODELS))[0])
        else:
            weights.append(fit_weights(sums[index] / counts[index]))
    return np.stack(weights), global_weights, sums, counts


def fuse(outputs, regime, weights, abstain=True):
    if (outputs.ndim != 3 or outputs.shape[0] != len(MODELS) or regime.shape != outputs.shape[1:]
            or weights.shape != (len(REGIMES), len(MODELS))
            or not np.isfinite(outputs).all() or not np.isfinite(weights).all()
            or (weights < -1e-12).any() or not np.allclose(weights.sum(axis=1), 1)
            or not np.isin(regime, np.arange(len(REGIMES))).all()):
        raise ValueError("Invalid convex fusion inputs")
    pixel_weights = weights[regime]
    result = np.sum(pixel_weights * np.moveaxis(outputs, 0, -1), axis=-1)
    if abstain:
        result = np.where(regime == 3, outputs[0], result)
    return result


def holdout_scenes(size=96):
    y, x = np.mgrid[:size, :size]
    return {
        "flat_mid_new": np.full((size, size), .22),
        "oblique_edge_27deg": np.where(x < .51 * y + 23, .08, .85),
        "curved_wire": np.where(np.abs(y - (48 + 11 * np.sin(x / 13))) < .65, .8, .12),
        "small_spots": np.where((x % 19 == 7) & (y % 23 == 9), .85, .15),
        "irregular_texture": .4 + .06 * np.sin(x * .8 + .015 * y ** 2) + .05 * np.cos(y * .65 + .007 * x ** 2),
        "low_contrast_bars": .4 + .025 * ((x // 3) % 2),
        "signed_hdr_surface": -.1 + 2.2 * (x / (size - 1)) ** 2 + .03 * y / (size - 1),
    }


def holdout_cases():
    for seed in (401, 419):
        rng = np.random.default_rng(seed)
        for scene, clean in holdout_scenes().items():
            for noise in ("gaussian", "correlated_gaussian", "impulse", "signal_dependent"):
                if noise == "impulse":
                    mask = rng.random(clean.shape) < .03
                    noisy = np.where(mask, rng.choice([-.1, 1.1], clean.shape), clean)
                elif noise == "signal_dependent":
                    noisy = clean + rng.normal(0, np.sqrt(np.maximum(clean, 0) / 2500 + .01 ** 2))
                else:
                    error = rng.normal(0, .025, clean.shape)
                    if noise == "correlated_gaussian":
                        error = gaussian_filter(error, .65, mode="mirror")
                        error *= .025 / np.std(error)
                    noisy = clean + error
                yield dict(split="holdout", seed=seed, scene=scene, noise=noise, clean=clean, noisy=noisy)


def run():
    training = list(cases("calibration"))
    weights, global_weights, sums, counts = calibrate(training)
    # Old validation is now a regression set, never called a fresh holdout.
    regression = [dict(case, split="regression") for case in cases("validation")]
    rows = []
    crop = np.s_[6:-6, 6:-6]
    for case in training + regression + list(holdout_cases()):
        outputs, regime, sigma = local_features(case["noisy"])
        combined = list(outputs) + [np.einsum("i,ihw->hw", global_weights, outputs),
                                   fuse(outputs, regime, weights, False), fuse(outputs, regime, weights, True)]
        structure = maximum_filter(np.hypot(*np.gradient(case["clean"])), 3)[crop] > .04
        labels = regime[crop]
        for method, output in zip(MODELS + ("global_fitted", "local_fitted", "local_abstention"), combined):
            error = output[crop] - case["clean"][crop]
            mse = error ** 2
            groups = {}
            for index, name in enumerate(REGIMES):
                mask = labels == index
                groups[name] = dict(pixels=int(mask.sum()),
                                    mse=float(mse[mask].mean()) if mask.any() else None)
            rows.append(dict(split=case["split"], scene=case["scene"], seed=case["seed"], noise=case["noise"],
                             method=method, pixels=int(error.size), estimated_sigma=sigma,
                             mse=float(mse.mean()), rmse=float(np.sqrt(mse.mean())),
                             mae=float(np.abs(error).mean()), mean_bias=float(error.mean()),
                             abstention_pixels=int((labels == 3).sum()) if method == "local_abstention" else 0,
                             structure_pixels=int(structure.sum()),
                             structure_mse=float(mse[structure].mean()) if structure.any() else None,
                             regimes=groups))
    return dict(global_weights=global_weights.tolist(), local_weights=weights.tolist(),
                calibration_error_product_sums=sums.tolist(), calibration_regime_pixels=counts.tolist(), rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    dependencies = (Path(__file__), Path(__file__).with_name("study_luminance_ensemble.py"))
    result = dict(campaign_id="local-luminance-v001-" + now.strftime("%Y%m%dT%H%M%SZ"),
                  schema_version=1, data_version=1, measured_at_utc=now.isoformat(),
                  revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                  source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dependencies},
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  configuration=dict(models=MODELS, regimes=REGIMES, size=[96, 96], crop_border=6,
                                     calibration_seeds=[101, 107], regression_seeds=[211, 223], holdout_seeds=[401, 419],
                                     holdout_scenes=list(holdout_scenes()),
                                     neighbourhoods="five 3x3 supports excluding centre + eight half-rays radius 3",
                                     guide_sigma=.8, tensor_sigma=1, structure_threshold_sigma=.35,
                                     coherence_threshold=.7, line_residual_sigma=3, line_support_sigma=2,
                                     sparse_regime_minimum_pixels=1024, numerical_sigma_floor=1e-8,
                                     abstention="uncertain regime uses input exactly",
                                     weights="nonnegative simplex MSE fit per noisy-observable regime on calibration only",
                                     noise="Gaussian .025; correlated Gaussian .65 px rescaled to .025; "
                                           "3% impulses -.1/1.1; Gaussian variance max(Y,0)/2500+.01^2",
                                     space="synthetic linear luminance; signed HDR; no clipping"),
                  audit=dict(production_modified=False, real_images_measured=False, posterior_calibrated=False,
                             true_noise_sigma_used_by_predictor=False, holdout_tuning=False), **run())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(f"{len(result['rows'])} measurements: {args.output}")
    print("Regime calibration pixels:", result["calibration_regime_pixels"])
    print("Local weights:", result["local_weights"])


if __name__ == "__main__":
    main()
