"""Lightroom's colour grading blending and balance: how they reshape the zone curves, and whether they add up.

For each zone alone at 30 degrees / saturation 50, across blending (0 to 100,
balance 0) and balance (-100 to +100, blending 50), the zone's weight curve over
the neutral's Oklab lightness is fitted with the zone's change vector fixed to
its value at the default blending and balance (etude-grading-v001): Delta =
w(L) c. Curves are piecewise linear on 21 knots; leave-one-scene-out share of
Lightroom's change left unexplained. A state measured on fewer than MIN_SCENES
scenes is not fitted.

The combined states (shadows 210 + highlights 30 across blending and at balance
-50 / +50; the three zones together) test the model w(L; blending, balance) =
w50,0 + (w_blending - w50,0) + (w_balance - w50,0), zones adding up, against the
default curves alone. Exports at 2048 pixels reduced by 2, as the grading study.
Writes one immutable, versioned JSON.

Usage: python tests/study_grading_blend.py --data-version N
"""
import argparse
import collections
import glob
import json
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis.native import embedded_metadata  # noqa: E402
from study_grading_response import KNOTS, load, oklab, position, weight  # noqa: E402
from study_sources import git  # noqa: E402

FIELDS = (("shadows", "SplitToningShadowHue", "SplitToningShadowSaturation"),
          ("midtones", "ColorGradeMidtoneHue", "ColorGradeMidtoneSat"),
          ("highlights", "SplitToningHighlightHue", "SplitToningHighlightSaturation"))
BLENDINGS = (0, 25, 50, 75, 100)
BALANCES = (-100, -50, -25, 0, 25, 50, 100)
MIN_SCENES = 20
STRIDE = 6


def state_of(xmp):
    zones = {zone: (int(xmp[h]), int(xmp[s])) for zone, h, s in FIELDS if int(xmp[s]) != 0}
    return zones, int(xmp["ColorGradeBlending"]), int(xmp["SplitToningBalance"])


def fit_curve(samples, c):
    """Least squares of the knots of w given Delta = w(L) c, tridiagonal normal equations."""
    n = len(KNOTS)
    diag, off, r = np.zeros(n), np.zeros(n - 1), np.zeros(n)
    norm = float(c @ c)
    for (i, t), d in samples:
        u, proj = 1 - t, d @ c
        diag += norm * (np.bincount(i, u * u, n) + np.bincount(i + 1, t * t, n))
        off += norm * np.bincount(i, u * t, n - 1)[:n - 1]
        r += np.bincount(i, u * proj, n) + np.bincount(i + 1, t * proj, n)
    return np.linalg.solve(np.diag(diag) + np.diag(off, 1) + np.diag(off, -1) + 1e-6 * np.eye(n), r)


def share(samples, predict):
    num = den = 0.0
    for (i, t), d in samples:
        num += float(np.sum((d - predict(i, t)) ** 2))
        den += float(np.sum(d ** 2))
    return num, den


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-grading-fusion-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    parent_path = Path(sorted(glob.glob(str(args.output_dir / "etude-grading-v001-*.json")))[-1])
    parent = json.loads(parent_path.read_text(encoding="utf-8"))["zones"]
    base = {z: np.array(parent[z]["curve"]) for z, _, _ in FIELDS}
    vector = {z: {k: np.array(v) for k, v in parent[z]["states"].items()} for z, _, _ in FIELDS}
    start = time.monotonic()
    root = args.base / "grading"
    neutrals = {}
    for p in sorted((root / "neutre").glob("*.tif")):
        rgb = load(p)
        neutrals[p.stem.split("-")[0]] = (rgb, oklab(rgb))
    singles = collections.defaultdict(dict)    # (zone, kind, value) -> scene -> samples
    combined = collections.defaultdict(dict)   # (zones, blending, balance) -> scene -> samples
    for p in sorted((root / "fusion").glob("*.tif")):
        _, xmp = embedded_metadata(p)
        zones, blending, balance = state_of(xmp)
        scene = p.stem.split("-")[0]
        rgb0, lab0 = neutrals[scene]
        rgb1 = load(p)
        ok = ((rgb0.max(2) < .98) & (rgb1.max(2) < .98)).ravel()
        idx = np.flatnonzero(ok)[::STRIDE]
        sample = (position(lab0[..., 0].ravel()[idx]), (oklab(rgb1) - lab0).reshape(-1, 3)[idx].astype(np.float32))
        if len(zones) == 1:
            zone = next(iter(zones))
            key = (zone, "blending", blending) if balance == 0 else (zone, "balance", balance)
            singles[key].setdefault(scene, sample)   # a repeated export counts once
        else:
            combined[(tuple(sorted(zones.items())), blending, balance)].setdefault(scene, sample)
    curves, report = {}, {}
    for zone, _, _ in FIELDS:
        c = vector[zone]["30_50"]
        curves[zone] = {"blending": {"50": base[zone].tolist()}, "balance": {"0": base[zone].tolist()}}
        for kind, values in (("blending", BLENDINGS), ("balance", BALANCES)):
            for value in values:
                if (kind == "blending" and value == 50) or (kind == "balance" and value == 0):
                    continue
                scenes = singles.get((zone, kind, value), {})
                if len(scenes) < MIN_SCENES:
                    report[f"{zone} {kind} {value}"] = {"scenes": len(scenes), "fitted": False}
                    print(f"{zone} {kind} {value:+d}: {len(scenes)} scenes, not fitted", flush=True)
                    continue
                num = den = 0.0
                for test in scenes:
                    w = fit_curve([scenes[s] for s in scenes if s != test], c)
                    a, b = share([scenes[test]], lambda i, t: np.outer(weight(w, i, t), c))
                    num, den = num + a, den + b
                w = fit_curve(list(scenes.values()), c)
                curves[zone][kind][str(value)] = w.round(4).tolist()
                a, b = 0.0, 0.0
                for s in scenes.values():
                    x, y = share([s], lambda i, t: np.outer(weight(base[zone], i, t), c))
                    a, b = a + x, b + y
                report[f"{zone} {kind} {value}"] = {"scenes": len(scenes), "fitted": True,
                                                    "leave_one_out_unexplained": num / den,
                                                    "unexplained_with_default_curve": a / b,
                                                    "peak_L": float(KNOTS[int(np.argmax(w))]), "peak": float(w.max())}
                print(f"{zone} {kind} {value:+d}: leave-one-out unexplained {num / den:.3f} "
                      f"(default curve {a / b:.3f}); peak L {KNOTS[int(np.argmax(w))]:.2f}, {w.max():.2f}", flush=True)
    # Additivity on the combined states.

    def model_curve(zone, blending, balance):
        w = base[zone].copy()
        b, v = curves[zone]["blending"].get(str(blending)), curves[zone]["balance"].get(str(balance))
        if b is None or v is None:
            return None
        return w + (np.array(b) - base[zone]) + (np.array(v) - base[zone])

    additivity = {}
    for (zones, blending, balance), scenes in sorted(combined.items()):
        label = "+".join(f"{z}{h}" for z, (h, s) in zones) + f" F{blending} B{balance:+d}"
        parts = [(z, vector[z][f"{h}_{s}"], model_curve(z, blending, balance)) for z, (h, s) in zones]
        if any(w is None for _, _, w in parts) or not scenes:
            additivity[label] = {"scenes": len(scenes), "tested": False}
            print(f"{label}: not tested", flush=True)
            continue
        modelled = lambda i, t: sum(np.outer(weight(w, i, t), c) for _, c, w in parts)  # noqa: E731
        default = lambda i, t: sum(np.outer(weight(base[z], i, t), c) for z, c, _ in parts)  # noqa: E731
        a, b = share(list(scenes.values()), modelled)
        x, _ = share(list(scenes.values()), default)
        additivity[label] = {"scenes": len(scenes), "tested": True, "unexplained_model": a / b,
                             "unexplained_default_curves": x / b}
        print(f"{label}: unexplained {a / b:.3f} with blending/balance, {x / b:.3f} with the default curves "
              f"({len(scenes)} scenes)", flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_grading_blend.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "parent_campaign": parent_path.name,
              "model": "w(L; blending, balance) = w50,0 + (w_blending - w50,0) + (w_balance - w50,0); zones add up",
              "curve_knots_L": KNOTS.tolist(), "min_scenes": MIN_SCENES, "sample_stride": STRIDE,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "curves": curves, "single_states": report, "additivity": additivity}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
