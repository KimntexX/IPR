"""Lightroom's dehaze as a gain in EV per channel, fitted on its neutral -> state pairs.

Lightroom's dehaze follows the haze: it acts where the dark channel is bright,
and its change of each channel depends on that channel against the airlight.
The haze model I = J t + A (1 - t) explains its change in linear light but
crushes the shadows to black where Lightroom lowers them by about one EV, so the
law is fitted in EV, shared by the scenes of a slider value:

    y' = y 2^f(log2(y / A), h),  f clamped to Lightroom's 1st-99th percentile change,
    h = Gaussian(dark channel, 128 px) / A, clamped to [0, 1],
    A = max(0.10, 99th percentile of max(R, G, B)), dark channel = min(R, G, B) eroded,

f bilinear on a grid of log2(y / A) and h, least squares in EV with a small
second-difference penalty (it carries f across the empty, dark and hazy corner).
Images at 1/4 resolution; leave-one-scene-out share of Lightroom's change of the
luminance in EV left unexplained. Writes one immutable, versioned JSON.

Usage: python tests/study_dehaze_law.py --data-version N
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
from analysis import inputs  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git  # noqa: E402

REDUCTION = 4
SMOOTH = 128 / REDUCTION       # Gaussian sigma of the haze, reduced pixels
ERODE = 15 // REDUCTION + 1    # dark channel window, reduced pixels
STRIDE = 5
TONE_KNOTS = np.array([-10, -8, -6, -5, -4, -3, -2.5, -2, -1.5, -1, -0.5, 0, 0.6])   # log2(y / A)
HAZE_KNOTS = np.array([0, .2, .4, .6, .8, 1])
SMOOTHNESS = 3e-3
WEIGHTS = np.array([.2126, .7152, .0722])


def prepare(neutral, variant):
    n = cv2.resize(neutral, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    v = cv2.resize(variant, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    airlight = max(0.10, float(np.quantile(n.max(axis=2), 0.99)))
    dark = cv2.erode(n.min(axis=2), np.ones((ERODE, ERODE), np.uint8))
    h = np.clip(cv2.GaussianBlur(dark, (0, 0), SMOOTH) / airlight, 0, 1)
    ok = ((n.max(2) < .98) & (v.max(2) < .98) & (n.min(2) > 1e-4)).ravel()
    index = np.flatnonzero(ok)[::STRIDE]
    return n.reshape(-1, 3)[index], v.reshape(-1, 3)[index], airlight, h.ravel()[index]


def weights(x, airlight, h):
    """Bilinear weights of each channel sample on the table: (cell index, weight) for 4 corners."""
    u = np.clip(np.log2(np.maximum(x, 1e-6) / airlight).ravel(), TONE_KNOTS[0], TONE_KNOTS[-1])
    hh = np.repeat(h[:, None], 3, 1).ravel()
    i = np.clip(np.searchsorted(TONE_KNOTS, u) - 1, 0, len(TONE_KNOTS) - 2)
    j = np.clip(np.searchsorted(HAZE_KNOTS, hh) - 1, 0, len(HAZE_KNOTS) - 2)
    a = (u - TONE_KNOTS[i]) / (TONE_KNOTS[i + 1] - TONE_KNOTS[i])
    b = (hh - HAZE_KNOTS[j]) / (HAZE_KNOTS[j + 1] - HAZE_KNOTS[j])
    n = len(HAZE_KNOTS)
    return [(i * n + j, (1 - a) * (1 - b)), ((i + 1) * n + j, a * (1 - b)),
            (i * n + j + 1, (1 - a) * b), ((i + 1) * n + j + 1, a * b)]


def penalty():
    n, m = len(TONE_KNOTS), len(HAZE_KNOTS)
    rows = []
    for i in range(n):
        for j in range(m):
            for di, dj in ((1, 0), (0, 1)):
                if i + 2 * di < n and j + 2 * dj < m:
                    r = np.zeros(n * m)
                    r[i * m + j] += 1
                    r[(i + di) * m + j + dj] -= 2
                    r[(i + 2 * di) * m + j + 2 * dj] += 1
                    rows.append(r)
    p = np.array(rows)
    return p.T @ p


def normal_equations(sample):
    x, y, airlight, h = sample
    target = np.log2(np.maximum(y, 1e-5) / x).ravel()
    design = np.zeros((target.size, len(TONE_KNOTS) * len(HAZE_KNOTS)))
    rows = np.arange(target.size)
    for k, w in weights(x, airlight, h):
        np.add.at(design, (rows, k), w)
    return design.T @ design / target.size, design.T @ target / target.size


def solve(parts):
    return np.linalg.solve(sum(p[0] for p in parts) + SMOOTHNESS * penalty(), sum(p[1] for p in parts))


def apply(table, bounds, x, airlight, h):
    f = np.zeros(x.size)
    for k, w in weights(x, airlight, h):
        f += w * table[k]
    return x * 2 ** np.clip(f, *bounds).reshape(x.shape)


def unexplained(prediction, x, y):
    lx, ly, lp = x @ WEIGHTS, y @ WEIGHTS, prediction @ WEIGHTS
    ok = lx > 1e-3
    lr = np.log2(np.maximum(ly[ok], 1e-4) / lx[ok])
    model = np.log2(np.maximum(lp[ok], 1e-4) / lx[ok])
    return float(np.sum((lr - model) ** 2)), float(np.sum(lr ** 2))


def bounds_of(samples):
    change = np.concatenate([np.log2(np.maximum(y, 1e-5) / x).ravel() for x, y, _, _ in samples])
    return float(np.quantile(change, 0.01)), float(np.quantile(change, 0.99))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-loi-voile-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    data = {}
    for r in inputs.base_records(args.base, ["effets"]):
        if r["feature"] == "dehaze" and r.get("reference"):
            data.setdefault(int(r["value"]), {})[r["scene"]] = prepare(
                linear(read_tiff(r["reference"])), linear(read_tiff(r["file"])))
            print(f"{r['scene']} {r['value']:+.0f}", flush=True)
    laws = {}
    for value, scenes in sorted(data.items()):
        parts = {s: normal_equations(v) for s, v in scenes.items()}
        num = den = 0.0
        per_scene = {}
        for test in scenes:
            others = [s for s in scenes if s != test]
            table = solve([parts[s] for s in others])
            x, y, airlight, h = scenes[test]
            a, b = unexplained(apply(table, bounds_of([scenes[s] for s in others]), x, airlight, h), x, y)
            per_scene[test] = a / b
            num, den = num + a, den + b
        table = solve(list(parts.values()))
        bounds = bounds_of(list(scenes.values()))
        laws[str(value)] = {"gain_ev": table.reshape(len(TONE_KNOTS), len(HAZE_KNOTS)).round(4).tolist(),
                            "gain_bounds_ev": [round(b, 4) for b in bounds],
                            "leave_one_out_unexplained": num / den,
                            "leave_one_out_unexplained_by_scene": per_scene, "scenes": sorted(scenes)}
        print(f"dehaze {value:+d}: leave-one-out unexplained {num / den:.2f} ("
              + ", ".join(f"{s} {u:.2f}" for s, u in per_scene.items()) + f"), bounds {bounds[0]:+.2f}/{bounds[1]:+.2f} EV",
              flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_dehaze_law.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "y' = y 2^f(log2(y/A), h), f bilinear, clamped to gain_bounds_ev",
              "tone_knots_log2_of_airlight": TONE_KNOTS.tolist(), "haze_knots": HAZE_KNOTS.tolist(),
              "gain_ev_layout": "rows tone knots, columns haze knots", "smoothness": SMOOTHNESS,
              "reduction": REDUCTION, "haze_smoothing_native_px": 128, "dark_channel_window_native_px": 15,
              "airlight": "max(0.10, 99th percentile of max RGB)", "sample_stride": STRIDE,
              "unexplained_metric": "share of Lightroom's luminance change in EV, sum of squares",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "laws": laws}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
