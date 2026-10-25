"""Detail gain of Lightroom's dehaze: how much it scales the local contrast, and what predicts it.

Lightroom's dehaze changes the contrast of every scale almost alike (its band
gains are nearly flat from 1 to 256 pixels: 0.08 to 0.62 at -100, 1.15 to 3.6 at
+100, depending on the scene), where a tone law applied pixel by pixel changes
mostly the middle scales. Per pixel, the local gain G of the detail of log2
luminance (below 16 pixels: L minus its Gaussian at 4 reduced pixels) is the
local regression of Lightroom's detail on the neutral's (Gaussian windows of 64
pixels). It is fitted per slider value as

    log2 G = c0 + ch h + cu u,

h the haze (dark channel smoothed at 128 pixels over A, A the 99th percentile
of the luminance), u = log2 of the local
mean luminance (64 pixels) over A, with equal weight per scene. Leave-one-scene-out
error over the within-scene variance, against a constant and h alone. Images at
1/4 resolution, log2(Y + 1e-3). Writes one immutable, versioned JSON.

Usage: python tests/study_dehaze_detail.py --data-version N
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

WEIGHTS = np.array([.2126, .7152, .0722])
REDUCTION = 4
WINDOW = 64 / REDUCTION        # local statistics, reduced pixels
DETAIL = 16 / REDUCTION        # detail below this scale
FLOOR = 1e-3
STRIDE = 9
MODELS = {"constant": [0], "haze": [0, 1], "haze_and_tone": [0, 1, 2]}


def samples(neutral, variant):
    n = cv2.resize(neutral, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    v = cv2.resize(variant, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    ok = ((n.max(2) < .98) & (v.max(2) < .98) & ((n @ WEIGHTS) > 1e-4)).astype(np.float32)
    airlight = max(.03, float(np.quantile(n @ WEIGHTS, .99)))   # as tests/study_dehaze_law.py
    dark = cv2.erode(n.min(2), np.ones((15 // REDUCTION + 1,) * 2, np.uint8))
    h = np.clip(cv2.GaussianBlur(dark, (0, 0), 128 / REDUCTION) / airlight, 0, 1)
    level = np.log2(n @ WEIGHTS + FLOOR).astype(np.float32)
    level_v = np.log2(v @ WEIGHTS + FLOOR).astype(np.float32)

    def mean(x):
        return cv2.GaussianBlur(x * ok, (0, 0), WINDOW) / np.maximum(cv2.GaussianBlur(ok, (0, 0), WINDOW), 1e-6)

    d = level - cv2.GaussianBlur(level, (0, 0), DETAIL)
    dv = level_v - cv2.GaussianBlur(level_v, (0, 0), DETAIL)
    variance = mean(d * d)
    gain = mean(d * dv) / np.maximum(variance, 1e-8)
    u = np.clip(mean(level) - np.log2(airlight + FLOOR), -10, .6)
    sel = (ok > 0) & (variance > 1e-4) & (gain > .02)
    x = np.stack([np.ones(int(sel.sum())), h[sel], u[sel]], axis=1)[::STRIDE]
    return x, np.log2(gain[sel])[::STRIDE]


def weighted_fit(parts, columns):
    x = np.concatenate([p[0][:, columns] / np.sqrt(len(p[1])) for p in parts])
    y = np.concatenate([p[1] / np.sqrt(len(p[1])) for p in parts])
    return np.linalg.lstsq(x, y, rcond=None)[0]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-detail-voile-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    data = {}
    for r in inputs.base_records(args.base, ["effets"]):
        if r["feature"] == "dehaze" and r.get("reference"):
            data.setdefault(int(r["value"]), {})[r["scene"]] = samples(
                linear(read_tiff(r["reference"])), linear(read_tiff(r["file"])))
    laws = {}
    for value, scenes in sorted(data.items()):
        scores = {}
        for name, columns in MODELS.items():
            num = den = 0.0
            median_error = []
            for test in scenes:
                c = weighted_fit([scenes[s] for s in scenes if s != test], columns)
                x, y = scenes[test]
                prediction = x[:, columns] @ c
                num += float(np.sum((y - prediction) ** 2))
                den += float(np.sum((y - y.mean()) ** 2))
                median_error.append(abs(float(np.median(y) - np.median(prediction))))
            scores[name] = {"error_over_within_scene_variance": num / den,
                            "mean_abs_error_of_scene_median_log2": float(np.mean(median_error))}
        c = weighted_fit(list(scenes.values()), MODELS["haze_and_tone"])
        laws[str(value)] = {"c0": float(c[0]), "ch": float(c[1]), "cu": float(c[2]),
                            "scene_median_log2_gain": {s: float(np.median(v[1])) for s, v in scenes.items()},
                            "leave_one_out": scores, "scenes": sorted(scenes)}
        print(f"dehaze {value:+d}: log2 G = {c[0]:+.3f} {c[1]:+.3f} h {c[2]:+.3f} u | "
              + ", ".join(f"{k} {v['mean_abs_error_of_scene_median_log2']:.2f}" for k, v in scores.items()), flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_dehaze_detail.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "log2 G = c0 + ch h + cu u, G local gain of log2 luminance detail below 16 px",
              "reduction": REDUCTION, "window_native_px": 64, "detail_native_px": 16, "log_floor": FLOOR,
              "u": "log2 of the local mean (64 px) over A, clamped to [-10, 0.6]", "sample_stride": STRIDE,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "laws": laws}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
