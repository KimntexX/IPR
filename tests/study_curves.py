"""Lightroom's point curves on its exports: in which space, with which interpolation, on which channels.

Exports of courbes (one state per preset; the curves read from the settings
Lightroom embeds: crs:ToneCurvePV2012, ...Red, ...Green, ...Blue, points in 0-255)
against the neutral of the same geometry (grading/neutre), reduced by 2. For each
export, the state is predicted from the neutral by the changed curve f, under every
combination of:

- space: encoded sRGB; linear ProPhoto with the sRGB transfer; ProPhoto gamma 1.8;
- interpolation of the points: natural cubic spline, monotone cubic (PCHIP),
  piecewise linear, Lagrange polynomial;
- mode (composite RGB curve only): per channel, or on the maximum channel with the
  others scaled alike (hue kept); a channel curve acts on its channel alone.

Share of Lightroom's change left unexplained (sum of squared residuals over sum of
squared changes, in the space's coordinates), per export and per family. Pixels
with a channel above 0.98 in the neutral or the state are left out. Writes one
immutable, versioned JSON.

Usage: python tests/study_curves.py --data-version N
"""
import argparse
import collections
import json
import platform
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from scipy.interpolate import CubicSpline, PchipInterpolator

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from study_point_color import embedded_xmp  # noqa: E402
from study_point_color_hsv import SRGB_TO_PROPHOTO  # noqa: E402
from study_sources import git  # noqa: E402

FIELDS = ("ToneCurvePV2012", "ToneCurvePV2012Red", "ToneCurvePV2012Green", "ToneCurvePV2012Blue")
CHANNEL = {"ToneCurvePV2012Red": 0, "ToneCurvePV2012Green": 1, "ToneCurvePV2012Blue": 2}
STRIDE = 8
CLIP = 0.98


def curves_of(xmp):
    """{field: [(x, y), ...] in [0, 1]} of the curves an export carries."""
    text = xmp.decode("utf-8", "ignore") if isinstance(xmp, (bytes, bytearray)) else xmp
    curves = {}
    for field in FIELDS:
        m = re.search(r"<crs:%s>(.*?)</crs:%s>" % (field, field), text, re.S)
        if m:
            points = [tuple(float(v) / 255.0 for v in li.split(",")) for li in re.findall(r"<rdf:li>([^<]*)</rdf:li>", m.group(1))]
            curves[field] = sorted(points)
    return curves


def is_linear(points):
    return all(abs(x - y) < 1e-9 for x, y in points)


def changed(curves):
    """The one curve moved off the identity, or None."""
    moved = [field for field, points in curves.items() if not is_linear(points)]
    return moved[0] if len(moved) == 1 else None


def interpolator(points, kind):
    x = np.array([p[0] for p in points])
    y = np.array([p[1] for p in points])
    if kind == "linear":
        return lambda v: np.interp(v, x, y)
    if kind == "natural":
        spline = CubicSpline(x, y, bc_type="natural")
    elif kind == "pchip":
        spline = PchipInterpolator(x, y)
    else:   # Lagrange polynomial through every point
        coefficients = np.polyfit(x, y, len(x) - 1)
        return lambda v: np.clip(np.polyval(coefficients, np.clip(v, x[0], x[-1])), 0.0, 1.0)
    # Flat beyond the end points, as an editor curve.
    return lambda v: np.clip(np.where(v < x[0], y[0], np.where(v > x[-1], y[-1], spline(np.clip(v, x[0], x[-1])))), 0.0, 1.0)


def srgb_encode(v):
    v = np.clip(v, 0.0, None)
    return np.where(v <= 0.0031308, 12.92 * v, 1.055 * np.power(v, 1 / 2.4) - 0.055)


SPACES = {
    "srgb": lambda linear: srgb_encode(linear),
    "prophoto_srgb_transfer": lambda linear: srgb_encode(linear @ SRGB_TO_PROPHOTO.T),
    "prophoto_gamma_1_8": lambda linear: np.power(np.clip(linear @ SRGB_TO_PROPHOTO.T, 0.0, None), 1 / 1.8),
}


def predict(space_values, f, field, mode):
    out = space_values.copy()
    if field in CHANNEL:
        c = CHANNEL[field]
        out[:, c] = f(space_values[:, c])
    elif mode == "per_channel":
        out = f(space_values)
    else:   # the maximum channel through the curve, the others scaled alike
        m = space_values.max(1)
        scale = np.where(m > 1e-6, f(m) / np.maximum(m, 1e-6), 1.0)
        out = space_values * scale[:, None]
    return out


def main(argv=None):
    from study_grading_response import load   # reads the TIFFs (OpenCV)
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-courbes-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    neutrals = {}
    for p in sorted((args.base / "grading" / "neutre").glob("*.tif")):
        flat = load(p).reshape(-1, 3).astype(np.float64)
        neutrals[p.stem.split("-")[0]] = flat
    rows, seen = [], set()
    kinds = ("natural", "pchip", "linear", "lagrange")
    for p in sorted((args.base / "courbes").glob("*.tif")):
        scene = p.stem.split("-")[0]
        curves = curves_of(embedded_xmp(p))
        field = changed(curves)
        if field is None or scene not in neutrals:
            continue
        key = (scene, field, tuple(curves[field]))
        if key in seen:
            continue
        seen.add(key)
        a = neutrals[scene]
        b = load(p).reshape(-1, 3).astype(np.float64)
        if b.shape != a.shape:
            raise ValueError("Geometry differs from the neutral: " + p.name)
        ok = (a.max(1) < CLIP) & (b.max(1) < CLIP)
        a, b = a[ok][::STRIDE], b[ok][::STRIDE]
        row = {"scene": scene, "curve": field, "points": [[round(x * 255), round(y * 255)] for x, y in curves[field]],
               "pixels": int(len(a)), "unexplained": {}}
        for space, convert in SPACES.items():
            sa, sb = convert(a), convert(b)
            change = float(np.sum((sb - sa) ** 2))
            for kind in kinds:
                f = interpolator(curves[field], kind)
                for mode in (("per_channel", "max_channel") if field == "ToneCurvePV2012" else ("channel",)):
                    residual = float(np.sum((predict(sa, f, field, mode) - sb) ** 2))
                    row["unexplained"][f"{space}/{kind}/{mode}"] = residual / max(change, 1e-12)
        rows.append(row)
        best = min(row["unexplained"].items(), key=lambda kv: kv[1])
        print(f"{p.name} {field} {row['points']}: best {best[0]} {best[1]:.4f}", flush=True)

    summary = collections.defaultdict(dict)
    families = collections.defaultdict(list)
    for r in rows:
        families["composite" if r["curve"] == "ToneCurvePV2012" else "channel"].append(r)
    for family, members in families.items():
        for combination in members[0]["unexplained"]:
            values = [m["unexplained"][combination] for m in members]
            summary[family][combination] = {"median": float(np.median(values)), "p90": float(np.quantile(values, .9)),
                                            "exports": len(values)}
    result = {"campaign_id": campaign, "runner": "tests/study_curves.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "metric": "sum of squared residuals over sum of squared changes, in the space's coordinates",
              "sample_stride": STRIDE, "clip": CLIP, "duration_seconds": round(time.monotonic() - start, 1),
              "python": platform.python_version(), "summary": summary, "rows": rows}
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    for family, combos in summary.items():
        print(f"\n{family}:")
        for combination, s in sorted(combos.items(), key=lambda kv: kv[1]["median"])[:8]:
            print(f"  {combination:45s} median {s['median']:.4f}  p90 {s['p90']:.4f}  ({s['exports']})")
    print(f"{output}\n{result['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
