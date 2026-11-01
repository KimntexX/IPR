"""Lightroom's colour grading luminance and global wheel: response curves and interaction with the colour.

Exports of grading/luminance against the neutral of the same geometry
(grading/neutre), in Oklab, reduced by 2 as the grading study:

- Luminance alone, per zone and for the global wheel, at each value: the change of a
  pixel is modelled as dL = g(L) and (da, db) = h(L) (a, b), g and h curves of the
  neutral's Oklab lightness (piecewise linear on 21 knots): a lightness shift and a
  chroma gain. Leave-one-scene-out share of Lightroom's change left unexplained,
  against the lightness shift alone (h = 0).
- The global wheel's colour (hue turn at saturation 50, saturation series at 30 and
  210 degrees): Delta = w(L) c, as the zones (study_grading_response.fit).
- Luminance -50 / +50 with a colour (30 degrees, saturation 50): does the luminance
  change add up with the colour's (zone curve and vector of etude-grading-v001, or of
  the global fit), the colour weighted at the neutral's lightness or at the lightness
  after the luminance shift?

Pixels with a channel above 0.98 in the neutral or the state are left out; a
repeated export counts once. Writes one immutable, versioned JSON.

Usage: python tests/study_grading_luminance.py --data-version N
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
from study_grading_response import KNOTS, fit, load, oklab, position, weight  # noqa: E402
from study_sources import git  # noqa: E402

ZONES = (("shadows", "ColorGradeShadowLum", "SplitToningShadowHue", "SplitToningShadowSaturation"),
         ("midtones", "ColorGradeMidtoneLum", "ColorGradeMidtoneHue", "ColorGradeMidtoneSat"),
         ("highlights", "ColorGradeHighlightLum", "SplitToningHighlightHue", "SplitToningHighlightSaturation"),
         ("global", "ColorGradeGlobalLum", "ColorGradeGlobalHue", "ColorGradeGlobalSat"))
STRIDE = 24
MIN_SCENES = 20


def state_of(xmp):
    """(zone, luminance, hue, saturation) of a single-zone state; None for anything else."""
    active = [(zone, int(xmp.get(lum, 0)), int(xmp.get(hue, 0)), int(xmp.get(sat, 0)))
              for zone, lum, hue, sat in ZONES if int(xmp.get(lum, 0)) or int(xmp.get(sat, 0))]
    if len(active) != 1 or int(xmp.get("ColorGradeBlending", 50)) != 50 or int(xmp.get("SplitToningBalance", 0)):
        return None
    return active[0]


def solve(i, t, multiplier, target):
    """Knots x minimising sum (target - multiplier * sum phi_k x_k)^2, phi the linear basis."""
    n = len(KNOTS)
    u, m2 = 1 - t, multiplier * multiplier
    diag = np.bincount(i, m2 * u * u, n) + np.bincount(i + 1, m2 * t * t, n)
    off = np.bincount(i, m2 * u * t, n - 1)[:n - 1]
    r = np.bincount(i, multiplier * u * target, n) + np.bincount(i + 1, multiplier * t * target, n)
    return np.linalg.solve(np.diag(diag) + np.diag(off, 1) + np.diag(off, -1) + 1e-7 * np.eye(n), r)


def luminance_fit(samples, chroma=True):
    i = np.concatenate([s["i"] for s in samples]).astype(np.int64)
    t = np.concatenate([s["t"] for s in samples])
    d = np.concatenate([s["d"] for s in samples])
    ab = np.concatenate([s["ab"] for s in samples])
    g = solve(i, t, np.ones_like(t), d[:, 0])
    if not chroma:
        return g, np.zeros_like(g)
    h = solve(np.concatenate([i, i]), np.concatenate([t, t]), np.concatenate([ab[:, 0], ab[:, 1]]),
              np.concatenate([d[:, 1], d[:, 2]]))
    return g, h


def luminance_predict(s, g, h):
    i, t = s["i"].astype(np.int64), s["t"]
    out = np.zeros_like(s["d"])
    out[:, 0] = weight(g, i, t)
    out[:, 1:] = weight(h, i, t)[:, None] * s["ab"]
    return out


def share(samples, predict):
    num = den = 0.0
    for s in samples:
        num += float(np.sum((s["d"] - predict(s)) ** 2))
        den += float(np.sum(s["d"] ** 2))
    return num, den


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-grading-luminance-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    parent_path = Path(sorted(glob.glob(str(args.output_dir / "etude-grading-v001-*.json")))[-1])
    parent = json.loads(parent_path.read_text(encoding="utf-8"))["zones"]
    start = time.monotonic()
    root = args.base / "grading"
    neutrals = {}
    for p in sorted((root / "neutre").glob("*.tif")):
        rgb = load(p)
        neutrals[p.stem.split("-")[0]] = (rgb, oklab(rgb))
    states = collections.defaultdict(dict)   # (zone, luminance, hue, saturation) -> scene -> samples
    for p in sorted((root / "luminance").glob("*.tif")):
        _, xmp = embedded_metadata(p)
        state = state_of(xmp)
        scene = p.stem.split("-")[0]
        if state is None or scene in states[state]:
            continue
        rgb0, lab0 = neutrals[scene]
        rgb1 = load(p)
        if rgb1.shape != rgb0.shape:
            raise ValueError("Geometry differs from the neutral: " + p.name)
        ok = ((rgb0.max(2) < .98) & (rgb1.max(2) < .98)).ravel()
        idx = np.flatnonzero(ok)[::STRIDE]
        lab = lab0.reshape(-1, 3)[idx]
        i, t = position(lab[:, 0])
        states[state][scene] = {"i": i, "t": t, "ab": lab[:, 1:].astype(np.float32),
                                "L": lab[:, 0].astype(np.float32),
                                "d": (oklab(rgb1).reshape(-1, 3)[idx] - lab).astype(np.float32)}
    print(f"{sum(len(v) for v in states.values())} exports in {len(states)} states", flush=True)

    luminance, luminance_report = collections.defaultdict(dict), {}
    for (zone, lum, hue, sat), scenes in sorted(states.items()):
        if sat or len(scenes) < MIN_SCENES:
            continue
        loo = {"shift_and_chroma": [0.0, 0.0], "shift_only": [0.0, 0.0]}
        for test in scenes:
            train = [scenes[s] for s in scenes if s != test]
            for name, chroma in (("shift_and_chroma", True), ("shift_only", False)):
                g, h = luminance_fit(train, chroma)
                a, b = share([scenes[test]], lambda s: luminance_predict(s, g, h))
                loo[name][0] += a
                loo[name][1] += b
        g, h = luminance_fit(list(scenes.values()))
        luminance[zone][str(lum)] = {"lightness_shift": g.round(5).tolist(), "chroma_gain": h.round(5).tolist()}
        luminance_report[f"{zone} {lum:+d}"] = {
            "scenes": len(scenes),
            "leave_one_out_unexplained": loo["shift_and_chroma"][0] / loo["shift_and_chroma"][1],
            "leave_one_out_unexplained_shift_only": loo["shift_only"][0] / loo["shift_only"][1],
            "peak_shift_L": float(KNOTS[int(np.argmax(np.abs(g)))]), "peak_shift": float(g[int(np.argmax(np.abs(g)))])}
        r = luminance_report[f"{zone} {lum:+d}"]
        print(f"{zone} L{lum:+d}: unexplained {r['leave_one_out_unexplained']:.3f} "
              f"(shift only {r['leave_one_out_unexplained_shift_only']:.3f}); shift peak "
              f"{r['peak_shift']:+.4f} at L {r['peak_shift_L']:.2f}; chroma gain at L .2/.5/.8 "
              f"{h[4]:+.3f}/{h[10]:+.3f}/{h[16]:+.3f}", flush=True)

    # The global wheel's colour, as a zone.
    global_samples = collections.defaultdict(list)   # scene -> [(state, (i, t), d)]
    for (zone, lum, hue, sat), scenes in states.items():
        if zone == "global" and sat and not lum:
            for scene, s in scenes.items():
                global_samples[scene].append(((hue, sat), (s["i"], s["t"]), s["d"]))
    num = den = 0.0
    for test in global_samples:
        curve, c = fit([x for sc, v in global_samples.items() if sc != test for x in v])
        for st, (i, t), d in global_samples[test]:
            if st in c:
                num += float(np.sum((d - np.outer(weight(curve, i, t), c[st])) ** 2))
                den += float(np.sum(d ** 2))
    global_curve, global_c = fit([x for v in global_samples.values() for x in v])
    global_report = {"scenes": len(global_samples), "leave_one_out_unexplained": num / den,
                     "curve": global_curve.round(4).tolist(),
                     "states": {f"{h}_{s}": [float(x) for x in v] for (h, s), v in sorted(global_c.items())},
                     "strength_over_50": {str(h): {str(s): float(np.linalg.norm(global_c[(h, s)]) /
                                                                 np.linalg.norm(global_c[(h, 50)]))
                                                   for s in (25, 50, 75, 100) if (h, s) in global_c}
                                          for h in (30, 210) if (h, 50) in global_c}}
    print(f"global colour: leave-one-out unexplained {num / den:.3f}; curve at L .1/.3/.5/.7/.9 "
          + "/".join(f"{global_curve[k]:.2f}" for k in (2, 6, 10, 14, 18))
          + "; strength over 50 at 30: " + " ".join(f"{s}:{v:.2f}" for s, v in global_report["strength_over_50"].get("30", {}).items()),
          flush=True)

    # Luminance with a colour: does it add up?
    colour_models = {z: (np.array(parent[z]["curve"]), {k: np.array(v) for k, v in parent[z]["states"].items()})
                     for z, _, _, _ in ZONES[:3]}
    colour_models["global"] = (global_curve, {f"{h}_{s}": v for (h, s), v in global_c.items()})
    interaction = {}
    for (zone, lum, hue, sat), scenes in sorted(states.items()):
        if not (sat and lum) or str(lum) not in luminance[zone]:
            continue
        lm = luminance[zone][str(lum)]
        g, h = np.array(lm["lightness_shift"]), np.array(lm["chroma_gain"])
        w, cs = colour_models[zone]
        c = cs.get(f"{hue}_{sat}")
        if c is None:
            continue
        before = lambda s: luminance_predict(s, g, h) + np.outer(weight(w, s["i"].astype(np.int64), s["t"]), c)  # noqa: E731

        def after(s):
            shifted = s["L"] + weight(g, s["i"].astype(np.int64), s["t"])
            i2, t2 = position(shifted)
            return luminance_predict(s, g, h) + np.outer(weight(w, i2.astype(np.int64), t2), c)
        a, b = share(list(scenes.values()), before)
        x, _ = share(list(scenes.values()), after)
        y, _ = share(list(scenes.values()), lambda s: luminance_predict(s, g, h))
        interaction[f"{zone} {lum:+d} {hue}/{sat}"] = {"scenes": len(scenes), "unexplained_added": a / b,
                                                       "unexplained_colour_after_shift": x / b,
                                                       "unexplained_luminance_alone": y / b}
        print(f"{zone} L{lum:+d} + {hue}/{sat}: unexplained added {a / b:.3f}, colour after the shift {x / b:.3f} "
              f"(luminance alone {y / b:.3f})", flush=True)

    record = {"campaign_id": campaign, "runner": "tests/study_grading_luminance.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "parent_campaign": parent_path.name,
              "model": "luminance: dL = g(L), (da, db) = h(L) (a, b); global colour: Delta = w(L) c",
              "curve_knots_L": KNOTS.tolist(), "min_scenes": MIN_SCENES, "sample_stride": STRIDE,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "luminance": luminance, "luminance_states": luminance_report, "global_colour": global_report,
              "interaction": interaction}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
