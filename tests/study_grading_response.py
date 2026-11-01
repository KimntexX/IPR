"""Lightroom's colour grading on its pairs: zone curves over lightness, colour per hue, strength per saturation.

For each zone (shadows, midtones, highlights) and state (hue, saturation), the
change of every pixel in Oklab, state minus the neutral of the same geometry
(2048-pixel exports, reduced again by 2), is modelled as

    Delta(pixel) = w_zone(L) c_state,

w_zone a curve of the neutral's Oklab lightness L shared by all states of the
zone (piecewise linear on 21 knots, peak 1), c_state a vector (dL, da, db). The
hue turn at saturation 50 gives the colour per hue, the saturation series at
30 and 210 degrees the strength per saturation: |c(hue, s)| / |c(hue, 50)|.
Fitted by alternating least squares; leave-one-scene-out share of Lightroom's
change left unexplained. Per scene, the lightness where each zone peaks is
recorded with the scene's key (median L), to see whether the zones follow the
image. Pixels with a channel above 0.98 in the neutral or the state are left
out. Writes one immutable, versioned JSON.

Usage: python tests/study_grading_response.py --data-version N
"""
import argparse
import collections
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
from analysis.native import embedded_metadata  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git  # noqa: E402

ZONES = {"shadows": ("SplitToningShadowHue", "SplitToningShadowSaturation"),
         "midtones": ("ColorGradeMidtoneHue", "ColorGradeMidtoneSat"),
         "highlights": ("SplitToningHighlightHue", "SplitToningHighlightSaturation")}
KNOTS = np.linspace(0, 1, 21)
REDUCTION = 2
STRIDE = 6
ITERATIONS = 8
# Oklab from linear sRGB (Ottosson 2020).
M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929], [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468], [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])


def oklab(rgb):
    return np.cbrt(np.maximum(rgb @ M1.T, 0)) @ M2.T


def load(path):
    rgb = linear(read_tiff(path)).astype(np.float32)
    return cv2.resize(rgb, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA)


def position(L):
    """Knot cell (uint8) and position in it (float32) of each lightness: a sparse linear basis."""
    L = np.clip(L, 0, 1)
    i = np.clip(np.searchsorted(KNOTS, L) - 1, 0, len(KNOTS) - 2)
    t = (L - KNOTS[i]) / (KNOTS[i + 1] - KNOTS[i])
    return i.astype(np.uint8), t.astype(np.float32)


def weight(curve, i, t):
    return curve[i] * (1 - t) + curve[i + 1] * t


def fit(samples):
    """samples: list of (state, (i, t), delta). Returns curve (knots) and c per state."""
    states = sorted({s for s, _, _ in samples})
    n = len(KNOTS)
    curve = np.exp(-((KNOTS - .5) / .25) ** 2)
    c = {}
    for _ in range(ITERATIONS):
        # c per state given the curve
        num = {s: np.zeros(3) for s in states}
        den = dict.fromkeys(states, 0.0)
        for st, (i, t), d in samples:
            w = weight(curve, i, t)
            num[st] += w @ d
            den[st] += float(w @ w)
        c = {s: num[s] / max(den[s], 1e-12) for s in states}
        # curve given c: the normal equations of a linear-interpolation basis are tridiagonal
        diag, off, r = np.zeros(n), np.zeros(n - 1), np.zeros(n)
        for st, (i, t), d in samples:
            cc = c[st]
            norm, proj, u = float(cc @ cc), d @ cc, 1 - t
            diag += norm * (np.bincount(i, u * u, n) + np.bincount(i + 1, t * t, n))
            off += norm * np.bincount(i, u * t, n - 1)[:n - 1]
            r += np.bincount(i, u * proj, n) + np.bincount(i + 1, t * proj, n)
        G = np.diag(diag) + np.diag(off, 1) + np.diag(off, -1)
        curve = np.linalg.solve(G + 1e-6 * np.eye(n), r)
        scale = max(curve.max(), 1e-9)
        curve /= scale
        c = {s: v * scale for s, v in c.items()}
    return curve, c


def unexplained(samples, curve, c):
    num = den = 0.0
    for st, (i, t), d in samples:
        p = np.outer(weight(curve, i, t), c[st])
        num += float(np.sum((d - p) ** 2))
        den += float(np.sum(d ** 2))
    return num, den


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-grading-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    root = args.base / "grading"
    neutrals = {}
    for p in sorted((root / "neutre").glob("*.tif")):
        rgb = load(p)
        neutrals[p.stem.split("-")[0]] = (rgb, oklab(rgb))
    keys = {s: float(np.median(lab[..., 0])) for s, (_, lab) in neutrals.items()}
    zones = {}
    for zone, (hue_field, saturation_field) in ZONES.items():
        per_scene = collections.defaultdict(list)
        for p in sorted((root / zone).glob("*.tif")):
            _, xmp = embedded_metadata(p)
            scene = p.stem.split("-")[0]
            state = (int(xmp[hue_field]), int(xmp[saturation_field]))
            rgb0, lab0 = neutrals[scene]
            rgb1 = load(p)
            if rgb1.shape != rgb0.shape:
                raise ValueError("Geometry differs from the neutral: " + p.name)
            ok = ((rgb0.max(2) < .98) & (rgb1.max(2) < .98)).ravel()
            idx = np.flatnonzero(ok)[::STRIDE]
            L = lab0[..., 0].ravel()[idx]
            delta = (oklab(rgb1) - lab0).reshape(-1, 3)[idx]
            per_scene[scene].append((state, position(L), delta.astype(np.float32)))
        print(f"{zone}: {sum(len(v) for v in per_scene.values())} states loaded", flush=True)
        num = den = 0.0
        by_scene = {}
        for test in per_scene:
            curve, c = fit([s for sc, v in per_scene.items() if sc != test for s in v])
            a, b = unexplained(per_scene[test], curve, c)
            num, den = num + a, den + b
            by_scene[test] = a / b
        curve, c = fit([s for v in per_scene.values() for s in v])
        # per-scene peak of the zone, for the adaptation check
        peaks = {}
        for scene, samples in per_scene.items():
            cs, _ = fit(samples)
            peaks[scene] = float(KNOTS[int(np.argmax(cs))])
        corr = float(np.corrcoef([keys[s] for s in peaks], list(peaks.values()))[0, 1])
        hue50 = {h: c[(h, 50)] for h in range(0, 360, 30) if (h, 50) in c}
        strength = {}
        for h in (30, 210):
            ref = np.linalg.norm(c[(h, 50)])
            strength[str(h)] = {str(s): float(np.linalg.norm(c[(h, s)]) / ref) for s in (10, 25, 50, 75, 100) if (h, s) in c}
        del per_scene
        zones[zone] = {
            "curve_knots_L": KNOTS.tolist(), "curve": curve.round(4).tolist(),
            "colour_per_hue_at_50": {str(h): {"dL": float(v[0]), "da": float(v[1]), "db": float(v[2]),
                                              "chroma": float(np.hypot(v[1], v[2])),
                                              "angle_deg": float(np.degrees(np.arctan2(v[2], v[1])) % 360)}
                                     for h, v in hue50.items()},
            "strength_over_50": strength,
            "states": {f"{h}_{s}": [float(x) for x in v] for (h, s), v in sorted(c.items())},
            "leave_one_out_unexplained": num / den, "leave_one_out_by_scene": by_scene,
            "peak_L_by_scene": peaks, "peak_vs_scene_key_correlation": corr}
        print(f"{zone}: leave-one-out unexplained {num / den:.3f} (worst scene {max(by_scene, key=by_scene.get)} "
              f"{max(by_scene.values()):.3f}); peak L {KNOTS[int(np.argmax(curve))]:.2f}; "
              f"peak vs scene key r = {corr:+.2f}", flush=True)
        print("   strength over 50 at 30 deg: " + " ".join(f"{s}:{v:.2f}" for s, v in strength["30"].items())
              + " | at 210: " + " ".join(f"{s}:{v:.2f}" for s, v in strength["210"].items()), flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_grading_response.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "Delta Oklab = w_zone(L) c_state; w piecewise linear on 21 knots of the neutral's Oklab L, peak 1",
              "exports": "Lightroom 2048-pixel 16-bit TIFF, neutral of the same geometry (grading/neutre)",
              "reduction": REDUCTION, "sample_stride": STRIDE, "clip_threshold": 0.98,
              "scenes": sorted(neutrals), "scene_key_median_L": keys,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "zones": zones}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
