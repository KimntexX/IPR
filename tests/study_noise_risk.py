"""Scene-block risk calibration, impulse detection and identity controls; study only."""
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
from study_luminance_ensemble import scene_set
from study_local_luminance import local_features, holdout_scenes

METHODS = ("unchanged", "weak_diffuse", "half_robust", "full_robust")
POLICIES = ("empirical_risk", "block_margin", "diffuse_guard")
NOISES = ("clean", "gaussian", "correlated_gaussian", "signal_dependent", "mixed_impulse")
THRESHOLDS = (0., .01, .05, .1, .25, .5, .75, .9, 1.)
MIN_BLOCKS = 6


def predict(plane):
    outputs, regimes, sigma = local_features(plane)
    med = outputs[1]
    z = np.abs(plane - med) / max(sigma, 1e-8)
    bins = 3 * regimes + np.digitize(z, (2., 4.))
    predictions = np.stack((plane, plane + .25 * (gaussian_filter(plane, .8, mode="mirror") - plane),
                            .5 * (plane + med), med))
    return predictions, bins, sigma


def source_scenes(split):
    if split == "calibration":
        return {**scene_set("calibration"), **scene_set("validation"), **holdout_scenes()}
    if split != "holdout":
        raise ValueError("Unknown split")
    y, x = np.mgrid[:96, :96]
    radius = np.hypot(x - 47.3, y - 45.8)
    return {
        "flat_012": np.full((96, 96), .12),
        "flat_075": np.full((96, 96), .75),
        "slanted_low_edge": np.where(x + .31 * y < 55, .36, .45),
        "elliptic_wire": np.where(np.abs(np.sqrt(((x - 46) / 1.3) ** 2 + (y - 44) ** 2) - 26) < .55, .8, .13),
        "random_star_points": np.where(np.random.default_rng(991).random((96, 96)) < .006, .9, .17),
        "dark_stitches": np.where((np.abs(y - (47 + 9 * np.sin(x / 11))) < .7) & (x % 9 < 5), .1, .65),
        "radial_texture": .4 + .05 * np.sin(radius * 1.7) + .035 * np.sin(x * .47 + y * .63),
        "soft_low_amplitude": .5 + .015 * np.sin(x * .9) * np.cos(y * .75),
        "hdr_saddle": -.07 + 1.8 * (x / 95) ** 2 - .15 * (y / 95) ** 2,
    }


def records(split):
    seeds = (503, 509) if split == "calibration" else (601, 617)
    for seed in seeds:
        rng = np.random.default_rng(seed)
        for scene, clean in source_scenes(split).items():
            for noise in NOISES:
                impulse = np.zeros(clean.shape, dtype=bool)
                if noise == "clean":
                    noisy = clean.copy()
                elif noise == "signal_dependent":
                    noisy = clean + rng.normal(0, np.sqrt(np.maximum(clean, 0) / 2500 + .01 ** 2))
                else:
                    error = rng.normal(0, .025, clean.shape)
                    if noise == "correlated_gaussian":
                        error = gaussian_filter(error, .65, mode="mirror")
                        error *= .025 / np.std(error)
                    noisy = clean + error
                    if noise == "mixed_impulse":
                        impulse = rng.random(clean.shape) < .03
                        noisy = np.where(impulse, rng.choice([-.1, 1.1], clean.shape), noisy)
                predictions, bins, sigma = predict(noisy)
                yield dict(split=split, seed=seed, scene=scene, noise=noise, clean=clean,
                           noisy=noisy, impulse=impulse, predictions=predictions, bins=bins, sigma=sigma)


def margin(values):
    """Diagnostic mean + 2 SE over scene blocks, NOT a certified risk bound."""
    values = np.asarray(values)
    if len(values) < MIN_BLOCKS:
        return None
    return float(values.mean() + 2 * values.std(ddof=1) / np.sqrt(len(values)))


def calibrate(data):
    crop = np.s_[6:-6, 6:-6]
    by_scene = {}
    counts = np.zeros(12, dtype=np.int64)
    impulses = np.zeros(12, dtype=np.int64)
    for case in data:
        labels = case["bins"][crop]
        base_error = (case["noisy"][crop] - case["clean"][crop]) ** 2
        delta = (case["predictions"][(slice(None),) + crop] - case["clean"][crop]) ** 2 - base_error
        group = by_scene.setdefault(case["scene"], np.zeros((2, 12, 5)))
        for cell in range(12):
            mask = labels == cell
            counts[cell] += mask.sum()
            impulses[cell] += case["impulse"][crop][mask].sum()
            for family in (0, 1):
                if family == 1 and case["noise"] == "mixed_impulse":
                    continue
                group[family, cell, :4] += delta[:, mask].sum(axis=1)
                group[family, cell, 4] += mask.sum()
    actions = {name: np.zeros(12, dtype=int) for name in POLICIES}
    cells = []
    for cell in range(12):
        all_blocks = [g[0, cell, :4] / g[0, cell, 4] for g in by_scene.values() if g[0, cell, 4] > 0]
        ordinary = [g[1, cell, :4] / g[1, cell, 4] for g in by_scene.values() if g[1, cell, 4] > 0]
        all_blocks = np.asarray(all_blocks)
        ordinary = np.asarray(ordinary)
        stats = []
        for method in range(len(METHODS)):
            mean = float(all_blocks[:, method].mean()) if len(all_blocks) else None
            score = margin(all_blocks[:, method]) if len(all_blocks) else None
            guard = margin(ordinary[:, method]) if len(ordinary) else None
            stats.append(dict(method=METHODS[method], delta_mean=mean, block_margin=score, ordinary_margin=guard))
        if len(all_blocks) >= MIN_BLOCKS:
            for policy in POLICIES:
                eligible = [0]
                for method in range(1, len(METHODS)):
                    stat = stats[method]
                    if policy == "empirical_risk" and stat["delta_mean"] < 0:
                        eligible.append(method)
                    elif policy != "empirical_risk" and stat["block_margin"] is not None and stat["block_margin"] < 0:
                        if policy != "diffuse_guard" or (stat["ordinary_margin"] is not None and stat["ordinary_margin"] <= 0):
                            eligible.append(method)
                actions[policy][cell] = min(eligible, key=lambda m: stats[m]["delta_mean"])
        cells.append(dict(bin=cell, pixels=int(counts[cell]), impulse_pixels=int(impulses[cell]),
                          scene_blocks=len(all_blocks), ordinary_scene_blocks=len(ordinary), risks=stats,
                          scene_deltas=[dict(scene=name, all_count=int(g[0, cell, 4]),
                                             all_delta_sums=g[0, cell, :4].tolist(),
                                             ordinary_count=int(g[1, cell, 4]), ordinary_delta_sums=g[1, cell, :4].tolist())
                                        for name, g in by_scene.items()]))
    # Empirical frequency on this corpus prior; no claim of calibrated posterior.
    probability = (impulses + 1) / (counts + 2)
    return actions, probability, cells


def apply_policy(predictions, bins, actions):
    if (predictions.ndim != 3 or predictions.shape[0] != len(METHODS)
            or bins.shape != predictions.shape[1:] or actions.shape != (12,)
            or not np.isin(actions, np.arange(len(METHODS))).all()
            or not np.isin(bins, np.arange(12)).all()):
        raise ValueError("Invalid risk policy inputs")
    return np.take_along_axis(predictions, actions[bins][None], axis=0)[0]


def run():
    training = list(records("calibration"))
    actions, probability, cells = calibrate(training)
    rows = []
    crop = np.s_[6:-6, 6:-6]
    for case in training + list(records("holdout")):
        outputs = list(case["predictions"]) + [apply_policy(case["predictions"], case["bins"], actions[p]) for p in POLICIES]
        labels = case["impulse"][crop]
        score = probability[case["bins"][crop]]
        brier = float(np.mean((score - labels) ** 2))
        confusion = []
        for threshold in THRESHOLDS:
            detected = score >= threshold
            confusion.append(dict(threshold=threshold, tp=int((detected & labels).sum()),
                                  fp=int((detected & ~labels).sum()), tn=int((~detected & ~labels).sum()),
                                  fn=int((~detected & labels).sum())))
        structure = maximum_filter(np.hypot(*np.gradient(case["clean"])), 3)[crop] > .04
        input_mse = float(np.mean((case["noisy"][crop] - case["clean"][crop]) ** 2))
        for method, output in zip(METHODS + POLICIES, outputs):
            error = output[crop] - case["clean"][crop]
            rows.append(dict(split=case["split"], scene=case["scene"], seed=case["seed"], noise=case["noise"],
                             method=method, pixels=int(error.size), sigma_estimated=case["sigma"],
                             input_mse=input_mse, mse=float(np.mean(error ** 2)), mae=float(np.mean(np.abs(error))),
                             mean_bias=float(error.mean()), modified_pixels=int((np.abs(output[crop] - case["noisy"][crop]) > 1e-12).sum()),
                             impulse_pixels=int(labels.sum()), nonimpulse_pixels=int((~labels).sum()),
                             impulse_mse=float(np.mean(error[labels] ** 2)) if labels.any() else None,
                             nonimpulse_mse=float(np.mean(error[~labels] ** 2)),
                             structure_pixels=int(structure.sum()),
                             structure_mse=float(np.mean(error[structure] ** 2)) if structure.any() else None,
                             detection_brier=brier if method == "unchanged" else None,
                             detection_confusion=confusion if method == "unchanged" else None))
    observed = np.full((17, 17), .2)
    observed[8, 8] = .9
    predictions, bins, _ = predict(observed)
    twins = {}
    for policy in POLICIES:
        value = float(apply_policy(predictions, bins, actions[policy])[8, 8])
        twins[policy] = dict(prediction=value, true_spot_squared_error=(value - .9) ** 2,
                             impulse_squared_error=(value - .2) ** 2)
    return dict(actions={p: actions[p].tolist() for p in POLICIES},
                impulse_frequency_by_bin=probability.tolist(), calibration_cells=cells,
                indistinguishable_pair=dict(observed_center=.9, background=.2, results=twins), rows=rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    sources = [Path(__file__), Path(__file__).with_name("study_local_luminance.py"),
               Path(__file__).with_name("study_luminance_ensemble.py")]
    result = dict(campaign_id="noise-risk-v001-" + now.strftime("%Y%m%dT%H%M%SZ"), schema_version=1, data_version=1,
                  measured_at_utc=now.isoformat(), revision=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                  source_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                  versions=dict(python=platform.python_version(), numpy=np.__version__, scipy=scipy.__version__),
                  configuration=dict(methods=METHODS, policies=POLICIES, noises=NOISES, size=[96, 96], crop_border=6,
                                     calibration_scenes=list(source_scenes("calibration")), calibration_seeds=[503, 509],
                                     holdout_scenes=list(source_scenes("holdout")), holdout_seeds=[601, 617],
                                     bins="3 * noisy-observable regime + abs(Y-nine_median)/sigma strata <2, [2,4), >=4",
                                     risk="mean scene delta MSE; conservative score mean+2SE, minimum 6 scene blocks",
                                     ordinary_guard="same score excluding mixed_impulse, including clean controls",
                                     detection="Laplace-smoothed empirical extra-impulse frequency per bin; corpus-dependent prior",
                                     gaussian_sigma=.025, correlation_blur_sigma=.65, impulse_probability=.03,
                                     impulse_values=[-.1, 1.1], heteroscedastic_variance="max(Y,0)/2500+.01^2",
                                     clean_controls=True, diffuse_blend=.25, half_robust_blend=.5,
                                     scale_floor=1e-8, thresholds=THRESHOLDS, space="linear scalar luminance; signed HDR; no clipping"),
                  audit=dict(production_modified=False, real_photographs_measured=False, sigma_oracle_used=False,
                             holdout_tuning=False, posterior_calibrated=False, formal_risk_bound=False,
                             note="Scene-block margin is diagnostic; selected models/multiple bins and scene dependence invalidate any safety guarantee"),
                  **run())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
    print(f"{len(result['rows'])} measurements: {args.output}")
    print("Actions:", result["actions"])
    print("Ambiguity control:", result["indistinguishable_pair"])


if __name__ == "__main__":
    main()
