"""Lightroom's clarity residual as a smooth local affine map of log luminance, and what predicts it.

After IPR's band model, what is left of Lightroom's clarity (its change of
log2 luminance minus IPR's, each against its own neutral, 1/4 resolution) is
fitted per scene as Delta = a(x) L + b(x), a and b smooth: a Gaussian-weighted
local least squares at a given radius (the structure of a bilateral grid of
affine coefficients, as in HDRNet). Its share explained is the ceiling of that
family on the scene's own pair.

Then a(x) and b(x) are predicted, across scenes, from features of the neutral
alone: local means of L at several radii, local standard deviation, and
global quantiles. A linear regression (the correlation matrix of features and
coefficients) is fitted leave-one-scene-out; the predicted map is applied to
the held-out scene. Scores are shares of the variance of Lightroom's change
left unexplained. Writes one immutable, versioned JSON.

Uses the reduced renders cached by tests/study_clarity_masking.py --cache.

Usage: python tests/study_clarity_affine_grid.py --data-version N --cache DIR
"""
import argparse
import json
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from study_clarity_adaptation import REDUCTION  # noqa: E402
from study_sources import git  # noqa: E402

GRID_RADII = (64, 128, 256)          # native pixels, smoothness of a(x), b(x)
FEATURE_RADII = (16, 64, 256)        # native pixels, local means of the level
STRIDE = 3


def blur(plane, radius):
    return cv2.GaussianBlur(plane.astype(np.float32), (0, 0), radius / REDUCTION, borderType=cv2.BORDER_REFLECT)


def affine_grid(level, target, valid, radius):
    """Local least squares of target on (L, 1) with Gaussian weights: a(x), b(x)."""
    w = valid.astype(np.float32)
    l, t = level * w, target * w
    s0, s1, s2 = blur(w, radius), blur(l, radius), blur(l * level, radius)
    st, slt = blur(t, radius), blur(l * target, radius)
    det = s0 * s2 - s1 * s1
    ok = det > 1e-6 * np.maximum(s0 * s0, 1e-12)
    a = np.where(ok, (s0 * slt - s1 * st) / np.where(ok, det, 1), 0)
    b = np.where(s0 > 1e-6, (st - a * s1) / np.maximum(s0, 1e-6), 0)
    return a, b


def features(level, valid):
    """Per pixel features of the neutral: local means, local deviation, global quantiles."""
    w = valid.astype(np.float32)
    columns, names = [np.ones_like(level)], ["1"]
    for r in FEATURE_RADII:
        mean = blur(level * w, r) / np.maximum(blur(w, r), 1e-6)
        columns.append(mean)
        names.append(f"mean_{r}")
    sq = blur(level * level * w, 64) / np.maximum(blur(w, 64), 1e-6)
    columns.append(np.sqrt(np.maximum(sq - columns[2] ** 2, 0)))
    names.append("std_64")
    q = np.quantile(level[valid], [0.1, 0.5, 0.9])
    for name, v in zip(("p10", "p50", "p90"), q):
        columns.append(np.full_like(level, v))
        names.append(name)
    return np.stack(columns, axis=-1), names


def share(change, model, lr, valid):
    left = (change - model)[valid]
    lr = lr[valid]
    return float(np.sum((left - left.mean()) ** 2)), float(np.sum((lr - lr.mean()) ** 2))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-clarte-grille-affine-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    states = {}
    for f in sorted(args.cache.glob("*.npz")):
        scene, value = f.stem.rsplit("_", 1)
        z = np.load(f)
        valid = (z["level"] > -11) & (z["lr1"] < 0)
        states.setdefault(int(value), {})[scene] = (z["level"], z["lr"], z["ipr"], valid)
    results = {}
    for value, scenes in sorted(states.items()):
        out = results.setdefault(str(value), {})
        for radius in GRID_RADII:
            # Ceiling: each scene's own smooth affine map.
            num = den = 0.0
            grids, feats = {}, {}
            for name, (level, lr, ipr, valid) in scenes.items():
                residual = lr - ipr
                a, b = affine_grid(level, residual, valid, radius)
                n, d = share(residual, a * level + b, lr, valid)
                num, den = num + n, den + d
                grids[name] = (a, b)
                feats[name] = features(level, valid)
            before = sum(share(lr - ipr, 0, lr, valid)[0] for level, lr, ipr, valid in scenes.values()) / den
            ceiling = num / den
            # Prediction of a and b from the neutral's features, leave one scene out.
            names = feats[next(iter(feats))][1]
            num = 0.0
            for test in scenes:
                xs, ya, yb = [], [], []
                for s in scenes:
                    if s == test:
                        continue
                    valid = scenes[s][3]
                    x = feats[s][0][valid][::STRIDE]
                    xs.append(x)
                    ya.append(grids[s][0][valid][::STRIDE])
                    yb.append(grids[s][1][valid][::STRIDE])
                x = np.concatenate(xs).astype(np.float64)
                ca = np.linalg.lstsq(x, np.concatenate(ya), rcond=None)[0]
                cb = np.linalg.lstsq(x, np.concatenate(yb), rcond=None)[0]
                level, lr, ipr, valid = scenes[test]
                f = feats[test][0].astype(np.float64)
                n, _ = share(lr - ipr, (f @ ca) * level + f @ cb, lr, valid)
                num += n
            predicted = num / den
            ca_all = np.linalg.lstsq(np.concatenate([feats[s][0][scenes[s][3]][::STRIDE] for s in scenes]).astype(np.float64),
                                     np.concatenate([grids[s][0][scenes[s][3]][::STRIDE] for s in scenes]), rcond=None)[0]
            cb_all = np.linalg.lstsq(np.concatenate([feats[s][0][scenes[s][3]][::STRIDE] for s in scenes]).astype(np.float64),
                                     np.concatenate([grids[s][1][scenes[s][3]][::STRIDE] for s in scenes]), rcond=None)[0]
            out[str(radius)] = {"before": before, "own_affine_grid": ceiling, "predicted_from_neutral": predicted,
                                "features": names, "a_coefficients": ca_all.tolist(), "b_coefficients": cb_all.tolist()}
            print(f"clarity {value:+d} grid {radius:3d} px: unexplained {before:.2f}, own grid {ceiling:.2f}, "
                  f"predicted from the neutral {predicted:.2f}", flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_clarity_affine_grid.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "residual = a(x) L + b(x), a, b Gaussian-weighted local least squares",
              "score": "share of the variance of Lightroom's change of log2 luminance left unexplained",
              "reduction": REDUCTION, "grid_radii_native_px": list(GRID_RADII),
              "feature_radii_native_px": list(FEATURE_RADII), "sample_stride": STRIDE,
              "scenes": sorted({s for v in states.values() for s in v}),
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "results": results}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
