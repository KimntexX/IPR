"""Contrast-limited adaptive histogram equalisation (CLAHE) as the adaptive tone of Lightroom's clarity.

What IPR's band model leaves of Lightroom's clarity is, per scene, a smooth
local affine map of log luminance whose coefficients depend on the content in a
non-linear way (etude-clarte-grille-affine-v001). CLAHE gives such a map: the
log2 luminance L of the neutral is equalised tile by tile, through each tile's
histogram clipped at a multiple of its mean count (the clipped excess spread
evenly), its cumulative distribution mapping L onto the image's range; the
tile curves are interpolated bilinearly between tile centres. Its change
Delta = L_equalised - L, optionally smoothed (the band model already renders
local contrast), is fitted to the residual with one strength per slider value.

Target and score as in tests/study_clarity_masking.py: Lightroom's change of log2
luminance minus IPR's, 1/4 resolution, share of the variance of Lightroom's
change left unexplained, leave-one-scene-out. Writes one immutable, versioned JSON.

Uses the reduced renders cached by tests/study_clarity_masking.py --cache.

Usage: python tests/study_clarity_clahe.py --data-version N --cache DIR
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

TILES = (128, 256, 512, 1024)       # tile side, native pixels
CLIPS = (1.5, 2.5, 4.0, 1e9)        # clip limit, multiple of the mean bin count (1e9: no clip)
SMOOTHING = (0, 0.5)                # Gaussian sigma of Delta, fraction of the tile side
BINS = 64


def tile_curves(level, valid, tile, clip, lo, hi):
    """Cumulative distribution at the bin edges of each tile, in [0, 1]: shape (ty, tx, BINS + 1)."""
    rows, cols = level.shape
    ty, tx = max(1, round(rows / tile)), max(1, round(cols / tile))
    curves = np.zeros((ty, tx, BINS + 1))
    position = np.clip((level - lo) / (hi - lo) * BINS, 0, BINS - 1e-6)
    for i in range(ty):
        for j in range(tx):
            ys = slice(i * rows // ty, (i + 1) * rows // ty)
            xs = slice(j * cols // tx, (j + 1) * cols // tx)
            p = position[ys, xs][valid[ys, xs]]
            h = np.bincount(p.astype(int), minlength=BINS).astype(np.float64) if p.size else np.ones(BINS)
            limit = clip * h.sum() / BINS
            excess = np.maximum(h - limit, 0).sum()
            h = np.minimum(h, limit) + excess / BINS
            cdf = np.concatenate([[0.0], np.cumsum(h)])
            curves[i, j] = cdf / cdf[-1]
    return curves


def equalise(level, valid, tile, clip):
    lo, hi = np.quantile(level[valid], [0.005, 0.995])
    hi = max(hi, lo + 1e-3)
    curves = tile_curves(level, valid, tile, clip, lo, hi)
    ty, tx = curves.shape[:2]
    rows, cols = level.shape
    position = np.clip((level - lo) / (hi - lo) * BINS, 0, BINS - 1e-6)
    b = position.astype(int)
    f = position - b
    # Tile centres and bilinear weights.
    cy = (np.arange(rows) + 0.5) / rows * ty - 0.5
    cx = (np.arange(cols) + 0.5) / cols * tx - 0.5
    y0 = np.clip(np.floor(cy).astype(int), 0, ty - 1)
    x0 = np.clip(np.floor(cx).astype(int), 0, tx - 1)
    y1, x1 = np.minimum(y0 + 1, ty - 1), np.minimum(x0 + 1, tx - 1)
    wy = np.clip(cy - y0, 0, 1)[:, None]
    wx = np.clip(cx - x0, 0, 1)[None, :]

    def at(yi, xi):
        lower = curves[yi[:, None], xi[None, :], b]
        upper = curves[yi[:, None], xi[None, :], b + 1]
        return lower + (upper - lower) * f

    cdf = ((1 - wy) * ((1 - wx) * at(y0, x0) + wx * at(y0, x1))
           + wy * ((1 - wx) * at(y1, x0) + wx * at(y1, x1)))
    return lo + (hi - lo) * cdf - level


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-clarte-clahe-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    states, levels = {}, {}
    for f in sorted(args.cache.glob("*.npz")):
        scene, value = f.stem.rsplit("_", 1)
        z = np.load(f)
        valid = (z["level"] > -11) & (z["lr1"] < 0)
        levels[scene] = (z["level"].astype(np.float64), valid)
        states.setdefault(int(value), {})[scene] = ((z["lr"] - z["ipr"]).astype(np.float64), z["lr"].astype(np.float64))
    results = {}
    for tile in TILES:
        for clip in CLIPS:
            deltas = {s: equalise(level, valid, tile / REDUCTION, clip) for s, (level, valid) in levels.items()}
            for smoothing in SMOOTHING:
                if smoothing:
                    sigma = smoothing * tile / REDUCTION
                    used = {s: cv2.GaussianBlur(d.astype(np.float32), (0, 0), sigma).astype(np.float64)
                            for s, d in deltas.items()}
                else:
                    used = deltas
                key = f"tile_{tile}_clip_{'none' if clip > 1e8 else clip}_smooth_{smoothing}"
                for value, scenes in sorted(states.items()):
                    parts = {}
                    for s, (residual, lr) in scenes.items():
                        valid = levels[s][1]
                        parts[s] = (used[s][valid], residual[valid], lr[valid])
                    before = after = total = 0.0
                    by_scene = {}
                    for test in parts:
                        num = sum(float(np.dot(parts[s][0], parts[s][1])) / len(parts[s][1]) for s in parts if s != test)
                        den = sum(float(np.dot(parts[s][0], parts[s][0])) / len(parts[s][1]) for s in parts if s != test)
                        strength = num / den if den > 0 else 0.0
                        d, residual, lr = parts[test]
                        left = residual - strength * d
                        lr_var = float(np.sum((lr - lr.mean()) ** 2))
                        b = float(np.sum((residual - residual.mean()) ** 2))
                        a = float(np.sum((left - left.mean()) ** 2))
                        by_scene[test] = {"before": b / lr_var, "after": a / lr_var}
                        before, after, total = before + b, after + a, total + lr_var
                    num = sum(float(np.dot(p[0], p[1])) / len(p[1]) for p in parts.values())
                    den = sum(float(np.dot(p[0], p[0])) / len(p[1]) for p in parts.values())
                    results.setdefault(key, {})[str(value)] = {
                        "before": before / total, "after": after / total, "strength": num / den, "by_scene": by_scene}
                line = " ".join(f"{v:+d} {results[key][str(v)]['after']:.2f}" for v in sorted(states))
                print(f"{key}: before {results[key][str(max(states))]['before']:.2f} at +{max(states)}; after {line}",
                      flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_clarity_clahe.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "residual = strength * Delta, Delta = CLAHE of the neutral log2 luminance minus itself, "
                       "optionally smoothed",
              "score": "share of the variance of Lightroom's change of log2 luminance left unexplained, "
                       "leave-one-scene-out, before (IPR band model) and after the CLAHE term",
              "reduction": REDUCTION, "tiles_native_px": list(TILES), "clips": list(CLIPS),
              "smoothing_fraction_of_tile": list(SMOOTHING), "bins": BINS,
              "range": "0.5 to 99.5 % of the valid log2 luminance", "scenes": sorted(levels),
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "results": results}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
