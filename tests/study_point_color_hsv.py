"""Lightroom's point colour in the HSV of linear ProPhoto RGB: neighbourhood trapezoids and slider responses.

Lightroom writes a point's source as three numbers; on the exports they match the
hue (radians, hexcone), saturation (max - min) / max and value max of linear
ProPhoto RGB, and saturation -100 keeps the value: the change of a colour is read
in that space. Exports of couleur-point against the neutral of the same geometry
(grading/neutre), reduced by 2:

- Weight: w = (1 - S/S0) / A under saturation -100, A the 99.5th percentile of
  1 - S/S0 at range 50 over all scenes (S0 above 0.05).
- Neighbourhood per source and range: w = k T_h(dH) T_s(dS) T_v(dV), each T a
  symmetric trapezoid (1 on a plateau of half-width p, linear to 0 over a ramp r),
  dH the hue difference (radians), dS = S - S_centre, dV = V^(1/2.2) - V_centre^(1/2.2)
  (Lightroom's luminance sub-range is centred there). The centre is Lightroom's
  source plus three fitted offsets: Lightroom writes its source in its own space,
  ahead of the rendering the exports carry. Least squares on a fixed random sample,
  RMS residual and leave-one-scene-out RMS.
- Responses at range 50, change at w = 1 by least squares through the origin:
  hue shift (radians), log saturation gain, log value gain; variance as the gain of
  the hue, saturation and V^(1/2.2) deviations from the centre fitted at range 50.
- Source check: the median HSV of the pixels at w >= 0.9 against Lightroom's numbers.

Pixels with a channel above 0.98 in the neutral or the state are left out; a
repeated export counts once. Writes one immutable, versioned JSON.

Usage: python tests/study_point_color_hsv.py --data-version N
"""
import argparse
import collections
import json
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from study_grading_response import load  # noqa: E402
from study_point_color import CLIP, MIN_PIXELS, embedded_xmp, point_state, single_slider, slope  # noqa: E402
from study_sources import git  # noqa: E402

STRIDE = 12
SAMPLE = 120000
MIN_SATURATION = 0.05
# Linear sRGB D65 -> linear ProPhoto D50 (Bradford), as WorkingColor::srgbToProPhoto.
SRGB_TO_XYZ = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750],
                        [0.0193339, 0.1191920, 0.9503041]])
BRADFORD = np.array([[1.0478112, 0.0228866, -0.0501270], [0.0295424, 0.9904844, -0.0170491],
                     [-0.0092345, 0.0150436, 0.7521316]])
XYZ_TO_PROPHOTO = np.linalg.inv(np.array([[0.7976749, 0.1351917, 0.0313534], [0.2880402, 0.7118741, 0.0000857],
                                          [0.0, 0.0, 0.8252100]]))
SRGB_TO_PROPHOTO = XYZ_TO_PROPHOTO @ BRADFORD @ SRGB_TO_XYZ


def hsv(linear_srgb):
    """Hue (radians, hexcone, [0, 2 pi)), saturation and value of linear ProPhoto RGB."""
    p = linear_srgb @ SRGB_TO_PROPHOTO.T
    high, low = p.max(-1), p.min(-1)
    delta = high - low
    r, g, b = p[..., 0], p[..., 1], p[..., 2]
    safe = np.where(delta > 1e-12, delta, 1.0)
    sector = np.where(high == r, ((g - b) / safe) % 6.0, np.where(high == g, (b - r) / safe + 2.0, (r - g) / safe + 4.0))
    hue = np.where(delta > 1e-12, sector * np.pi / 3.0, 0.0)
    saturation = np.where(high > 1e-9, delta / np.where(high > 1e-9, high, 1.0), 0.0)
    return hue, saturation, high


def hue_difference(h, reference):
    return (h - reference + np.pi) % (2 * np.pi) - np.pi


def distances(h, s, v, source):
    hs, ss, vs = source
    return np.stack([hue_difference(h, hs), s - ss, np.maximum(v, 0) ** (1 / 2.2) - max(vs, 0) ** (1 / 2.2)])


def trapezoid(d, plateau, ramp):
    return np.clip(1.0 - (np.abs(d) - plateau) / np.maximum(ramp, 1e-6), 0.0, 1.0)


def predict(d, p):
    """p: scale, centre offsets (hue, saturation, encoded value), then plateau and ramp per axis."""
    k, oh, os_, ov, ph, rh, ps, rs, pv, rv = p
    return (k * trapezoid(hue_difference(d[0], oh), ph, rh) * trapezoid(d[1] - os_, ps, rs)
            * trapezoid(d[2] - ov, pv, rv))


def fit(d, w, start):
    lower = [0.5, -0.5, -0.4, -0.4, 0.0, 0.01, 0.0, 0.01, 0.0, 0.01]
    upper = [1.5, 0.5, 0.4, 0.4, 3.0, 3.0, 1.0, 2.0, 1.0, 2.0]
    return least_squares(lambda p: predict(d, p) - w, np.clip(start, lower, upper), bounds=(lower, upper),
                         x_scale="jac").x


def rms(d, w, p):
    return float(np.sqrt(np.mean((predict(d, p) - w) ** 2)))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-couleur-point-tsv-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    neutrals = {}
    for p in sorted((args.base / "grading" / "neutre").glob("*.tif")):
        flat = load(p).reshape(-1, 3)
        idx = np.flatnonzero(flat.max(1) < CLIP)[::STRIDE]
        neutrals[p.stem.split("-")[0]] = (idx, np.stack(hsv(flat[idx].astype(np.float64))))
    exports = collections.defaultdict(lambda: collections.defaultdict(dict))   # source -> state -> scene -> hsv
    for p in sorted((args.base / "couleur-point").glob("*.tif")):
        state = point_state(embedded_xmp(p))
        scene = p.stem.split("-")[0]
        if state is None or scene not in neutrals:
            continue
        key = (state["hue"], state["saturation"], state["luminance"], state["variance"], state["range"])
        if scene in exports[state["source"]][key]:
            continue
        idx, _ = neutrals[scene]
        flat = load(p).reshape(-1, 3)[idx].astype(np.float64)
        values = np.stack(hsv(flat))
        values[:, flat.max(1) >= CLIP] = np.nan
        exports[state["source"]][key][scene] = values.astype(np.float32)
    print(f"{sum(len(s) for v in exports.values() for s in v.values())} exports, {len(exports)} sources", flush=True)

    rng = np.random.default_rng(20261028)
    results = {}
    for source, states in sorted(exports.items()):
        label = "/".join(f"{v:.4f}" for v in source)

        def loss_of(scene, values):
            s0, s1 = neutrals[scene][1][1], values[1]
            return np.where(s0 > MIN_SATURATION, 1.0 - s1 / np.maximum(s0, 1e-9), np.nan)

        reference = states.get((0, -100, 0, 0, 50), {})
        if not reference:
            continue
        losses = {scene: loss_of(scene, values) for scene, values in reference.items()}
        pooled = np.concatenate(list(losses.values()))
        peak = float(np.nanpercentile(pooled, 99.5))
        weights = {scene: np.clip(loss / peak, 0.0, 1.0) for scene, loss in losses.items()}
        hsv0 = np.concatenate([neutrals[s][1] for s in weights], axis=1)
        w_all = np.concatenate(list(weights.values()))
        core = np.isfinite(w_all) & (w_all >= 0.9)
        record = {"lightroom_source": list(source), "saturation_minus_100_amplitude": round(peak, 4),
                  "core_pixels_hsv_median": None, "ranges": {}, "responses": {}}
        if core.sum() >= MIN_PIXELS:
            hue_core = hsv0[0][core]
            centre = float(np.angle(np.mean(np.exp(1j * hue_core))) % (2 * np.pi))
            record["core_pixels_hsv_median"] = [round(centre, 4), round(float(np.median(hsv0[1][core])), 4),
                                                round(float(np.median(hsv0[2][core])), 4)]
        for (hue, sat, lum, var, range_), scenes in sorted(states.items()):
            if (hue, sat, lum, var) != (0, -100, 0, 0):
                continue
            names = sorted(scenes)
            d = np.concatenate([distances(*neutrals[s][1], source) for s in names], axis=1)
            w = np.concatenate([np.clip(loss_of(s, scenes[s]) / peak, 0.0, 1.0) for s in names])
            scene_of = np.concatenate([[s] * scenes[s].shape[1] for s in names])
            ok = np.isfinite(w) & np.all(np.isfinite(d), axis=0)
            d, w, scene_of = d[:, ok], w[ok], scene_of[ok]
            pick = rng.choice(len(w), size=min(SAMPLE, len(w)), replace=False)
            d, w, scene_of = d[:, pick], w[pick], scene_of[pick]
            parameters = fit(d, w, [1.0, 0.0, 0.0, 0.0, 0.2, 0.55, 0.19, 0.4, 0.16, 0.4])
            held = []
            for scene in names[::4]:
                test = scene_of == scene
                if test.sum() < 500:
                    continue
                p = fit(d[:, ~test], w[~test], parameters)
                held.append(rms(d[:, test], w[test], p))
            record["ranges"][str(range_)] = {
                "scenes": len(names), "scale": round(float(parameters[0]), 4),
                "centre_offsets": [round(float(v), 4) for v in parameters[1:4]],
                "hue_plateau_ramp": [round(float(v), 4) for v in parameters[4:6]],
                "saturation_plateau_ramp": [round(float(v), 4) for v in parameters[6:8]],
                "value_plateau_ramp": [round(float(v), 4) for v in parameters[8:10]],
                "rms": round(rms(d, w, parameters), 5),
                "held_out_rms_median": round(float(np.median(held)), 5) if held else None}
            print(f"{label} G{range_}: {record['ranges'][str(range_)]}", flush=True)
        # The neighbourhood's centre, in IPR's HSV of the rendered image: from the range 50 fit.
        centre = list(source)
        if "50" in record["ranges"]:
            oh, os_, ov = record["ranges"]["50"]["centre_offsets"]
            encoded = max(source[2], 0.0) ** (1 / 2.2) + ov
            centre = [float((source[0] + oh) % (2 * np.pi)), source[1] + os_, max(encoded, 0.0) ** 2.2]
        record["centre_hsv"] = [round(v, 5) for v in centre]
        for key, scenes in sorted(states.items()):
            moved = single_slider(dict(zip(("hue", "saturation", "luminance", "variance", "range"), key)))
            if moved is None or moved == ("saturation", -100):
                continue
            component, value = moved
            common = [s for s in scenes if s in weights]
            if not common:
                continue
            w = np.concatenate([weights[s] for s in common])
            h0 = np.concatenate([neutrals[s][1][0] for s in common]); h1 = np.concatenate([scenes[s][0] for s in common])
            s0 = np.concatenate([neutrals[s][1][1] for s in common]); s1 = np.concatenate([scenes[s][1] for s in common])
            v0 = np.concatenate([neutrals[s][1][2] for s in common]); v1 = np.concatenate([scenes[s][2] for s in common])
            chromatic = s0 > MIN_SATURATION
            entry = {"scenes": len(common)}
            if component == "hue":
                entry["hue_radians"] = slope(w, np.where(chromatic, hue_difference(h1, h0), np.nan))
            elif component == "saturation":
                entry["log_saturation"] = slope(w, np.where(chromatic, np.log(np.maximum(s1, 1e-6) / s0), np.nan))
            elif component == "luminance":
                entry["log_value"] = slope(w, np.log(np.maximum(v1, 1e-6) / np.maximum(v0, 1e-6)))
                entry["log_saturation"] = slope(w, np.where(chromatic, np.log(np.maximum(s1, 1e-6) / s0), np.nan))
            else:
                dh0, dh1 = hue_difference(h0, centre[0]), hue_difference(h1, centre[0])
                ds0, ds1 = s0 - centre[1], s1 - centre[1]
                ec = max(centre[2], 0) ** (1 / 2.2)
                dv0, dv1 = np.maximum(v0, 0) ** (1 / 2.2) - ec, np.maximum(v1, 0) ** (1 / 2.2) - ec
                okh = chromatic & (np.abs(dh0) > 0.03)
                oks = chromatic & (np.abs(ds0) > 0.03)
                okv = chromatic & (np.abs(dv0) > 0.03)
                entry["hue_deviation_gain"] = slope(w, np.where(okh, dh1 / np.where(okh, dh0, 1.0) - 1.0, np.nan))
                entry["saturation_deviation_gain"] = slope(w, np.where(oks, ds1 / np.where(oks, ds0, 1.0) - 1.0, np.nan))
                entry["value_deviation_gain"] = slope(w, np.where(okv, dv1 / np.where(okv, dv0, 1.0) - 1.0, np.nan))
            record["responses"].setdefault(component, {})[str(value)] = entry
        results[label] = record
        print(f"{label}: core HSV {record['core_pixels_hsv_median']}; responses {record['responses']}", flush=True)

    out = {"campaign_id": campaign, "runner": "tests/study_point_color_hsv.py",
           "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
           "space": "HSV of linear ProPhoto RGB (D50, Bradford from sRGB D65); hue in radians",
           "model": "w = k T_h(dH) T_s(dS) T_v(dV), symmetric trapezoids around a fitted centre; dV on V^(1/2.2)",
           "sample_stride": STRIDE, "sample_pixels": SAMPLE, "seed": 20261028,
           "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
           "sources": results}
    output.write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{out['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
