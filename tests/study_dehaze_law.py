"""Lightroom's dehaze: a gain in EV of the luminance, and the colour of the airlight, fitted on its pairs.

Lightroom's dehaze follows the haze: it acts where the dark channel is bright,
and its change depends on the tone against the airlight. The haze model
I = J t + A (1 - t) explains its change in linear light but crushes the shadows
to black where Lightroom lowers them by about one EV, so the luminance law is
fitted in EV, shared by the scenes of a slider value:

    Y' = Y 2^f(log2(Y / A), h),  f clamped to Lightroom's 1st-99th percentile change,
    h = Gaussian(dark channel, 128 px) / A, clamped to [0, 1],
    A = max(0.03, 99th percentile of the luminance), dark channel = min(R, G, B) eroded,

f bilinear on a grid of log2(Y / A) and h, least squares in EV with a small
second-difference penalty (it carries f across the empty, dark and hazy corner).
A gain per channel shifted the colours of dark scenes (blue lowered by 2 EV
against green); the luminance gain keeps the channel ratios.

Colour: Lightroom removes or adds light of the airlight's own colour (a blue
cast removed from 6V1A0327, a warm one from _MG_7580). The airlight colour a is
the mean RGB of the 0.1 % most hazy pixels (brightest dark channel, He et al.
2009), normalised to luminance 1. When haze is added (negative values), a share
beta of the luminance change is light of that colour:

    ratio-kept   r = x Y' / Y,
    airlight     z = max(x + (Y' - Y) a, 0), rescaled to luminance Y',
    output       (1 - beta) r + beta z.

When haze is removed (positive values), that model and a gain per channel both
explain less of Lightroom's colour than no change at all; Lightroom saturates
where there is haze and removes its cast, so the log ratios to green, taken
from those of the airlight colour, d = log2(R/G) - log2(a_R/a_G) (and B), gain
kappa h d / (1 + (d / R0)^2) at constant luminance: faint colours of the haze
are expanded away from its cast, strong ones barely (centred on grey, the cast
grew: blue against green -0.5 EV on _MG_7580 where Lightroom removed it) (unbounded, a strong colour in haze grew by
4.5 EV against 1.9 at most in Lightroom). beta and kappa per slider value are fitted
on the change of those log ratios with Lightroom's own luminance change, so
that colour is judged apart from tone.

Images at 1/4 resolution; leave-one-scene-out shares of Lightroom's change left
unexplained: luminance in EV, and colour ratios. Writes one immutable, versioned JSON.

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
TONE_KNOTS = np.array([-10, -8, -6, -5, -4, -3, -2.5, -2, -1.5, -1, -0.5, 0, 0.6])   # log2(Y / A)
HAZE_KNOTS = np.array([0, .2, .4, .6, .8, 1])
SMOOTHNESS = 3e-3
WEIGHTS = np.array([.2126, .7152, .0722])
HAZIEST = 0.001                # share of pixels giving the airlight colour
BETAS = np.linspace(0, 1, 21)
RATIO_SCALE = 1.0             # R0, EV


def prepare(neutral, variant):
    n = cv2.resize(neutral, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    v = cv2.resize(variant, None, fx=1 / REDUCTION, fy=1 / REDUCTION, interpolation=cv2.INTER_AREA).astype(np.float32)
    # Of the luminance: saturated colours (red climbing holds) put max RGB far above the
    # brightest tones and the law then lifted them (6V1A9539, etude-loi-voile-v008).
    airlight = max(0.03, float(np.quantile(n @ WEIGHTS, 0.99)))
    dark = cv2.erode(n.min(axis=2), np.ones((ERODE, ERODE), np.uint8))
    hazy = dark >= np.quantile(dark, 1 - HAZIEST)
    colour = n[hazy].mean(axis=0).astype(np.float64)
    colour /= max(float(colour @ WEIGHTS), 1e-6)
    h = np.clip(cv2.GaussianBlur(dark, (0, 0), SMOOTH) / airlight, 0, 1)
    ok = ((n.max(2) < .98) & (v.max(2) < .98) & (n.min(2) > 1e-4)).ravel()
    index = np.flatnonzero(ok)[::STRIDE]
    return (n.reshape(-1, 3)[index].astype(np.float64), v.reshape(-1, 3)[index].astype(np.float64),
            airlight, h.ravel()[index], colour)


def weights(y, airlight, h):
    """Bilinear weights of each sample on the table: (cell index, weight) for 4 corners."""
    u = np.clip(np.log2(np.maximum(y, 1e-6) / airlight), TONE_KNOTS[0], TONE_KNOTS[-1])
    i = np.clip(np.searchsorted(TONE_KNOTS, u) - 1, 0, len(TONE_KNOTS) - 2)
    j = np.clip(np.searchsorted(HAZE_KNOTS, h) - 1, 0, len(HAZE_KNOTS) - 2)
    a = (u - TONE_KNOTS[i]) / (TONE_KNOTS[i + 1] - TONE_KNOTS[i])
    b = (h - HAZE_KNOTS[j]) / (HAZE_KNOTS[j + 1] - HAZE_KNOTS[j])
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
    x, y, airlight, h, _ = sample
    lx, ly = x @ WEIGHTS, y @ WEIGHTS
    target = np.log2(np.maximum(ly, 1e-5) / lx)
    design = np.zeros((target.size, len(TONE_KNOTS) * len(HAZE_KNOTS)))
    rows = np.arange(target.size)
    for k, w in weights(lx, airlight, h):
        np.add.at(design, (rows, k), w)
    return design.T @ design / target.size, design.T @ target / target.size


def solve(parts):
    return np.linalg.solve(sum(p[0] for p in parts) + SMOOTHNESS * penalty(), sum(p[1] for p in parts))


def luminance_gain(table, bounds, x, airlight, h):
    lx = x @ WEIGHTS
    f = np.zeros(lx.size)
    for k, w in weights(lx, airlight, h):
        f += w * table[k]
    return lx, lx * 2 ** np.clip(f, *bounds)


def recolour(x, lx, target, colour, beta):
    """Output RGB of luminance target: ratio-kept, plus a share beta of airlight-coloured light."""
    kept = x * (target / np.maximum(lx, 1e-9))[:, None]
    if beta == 0:
        return kept
    z = np.maximum(x + (target - lx)[:, None] * colour[None, :], 0)
    z *= (target / np.maximum(z @ WEIGHTS, 1e-9))[:, None]
    return (1 - beta) * kept + beta * z


def saturate(rgb, h, kappa, colour):
    """Log ratios to green d, from the airlight's, gain kappa h d / (1 + (d / R0)^2), luminance kept."""
    if kappa == 0:
        return rgb
    centre = np.log2(colour[[0, 2]] / colour[1])
    ok = rgb.min(axis=1) > 1e-6
    out = rgb.copy()
    x = rgb[ok]
    d = np.log2(x[:, [0, 2]] / x[:, [1]]) - centre
    r = centre + d + kappa * h[ok][:, None] * d / (1 + (d / RATIO_SCALE) ** 2)
    g = (x @ WEIGHTS) / (WEIGHTS[0] * 2 ** r[:, 0] + WEIGHTS[1] + WEIGHTS[2] * 2 ** r[:, 1])
    out[ok] = np.stack([g * 2 ** r[:, 0], g, g * 2 ** r[:, 1]], axis=1)
    return out


def luminance_unexplained(prediction, x, y):
    lx, ly, lp = x @ WEIGHTS, y @ WEIGHTS, prediction @ WEIGHTS
    ok = lx > 1e-3
    lr = np.log2(np.maximum(ly[ok], 1e-4) / lx[ok])
    model = np.log2(np.maximum(lp[ok], 1e-4) / lx[ok])
    return float(np.sum((lr - model) ** 2)), float(np.sum(lr ** 2))


def ratios(rgb):
    rgb = np.maximum(rgb, 1e-6)
    return np.stack([np.log2(rgb[:, 0] / rgb[:, 1]), np.log2(rgb[:, 2] / rgb[:, 1])], axis=1)


def colour_unexplained(sample, beta, kappa=0.0):
    """Change of log2(R/G), log2(B/G): model (with Lightroom's luminance) against Lightroom."""
    x, y, _, h, colour = sample
    ok = (x.min(1) > 1e-3) & (y.min(1) > 1e-3)
    x, y, h = x[ok], y[ok], h[ok]
    lr = ratios(y) - ratios(x)
    model = ratios(saturate(recolour(x, x @ WEIGHTS, y @ WEIGHTS, colour, beta), h, kappa, colour)) - ratios(x)
    return float(np.sum((lr - model) ** 2)), float(np.sum(lr ** 2))


def bounds_of(samples):
    change = np.concatenate([np.log2(np.maximum(y @ WEIGHTS, 1e-5) / (x @ WEIGHTS)) for x, y, _, _, _ in samples])
    return float(np.quantile(change, 0.01)), float(np.quantile(change, 0.99))


def best_beta(samples):
    costs = [sum(colour_unexplained(s, beta)[0] for s in samples) for beta in BETAS]
    return float(BETAS[int(np.argmin(costs))])


def best_kappa(samples):
    """Least squares of Lightroom's change of log ratios on kappa h d / (1 + (d / R0)^2)."""
    num = den = 0.0
    for x, y, _, h, colour in samples:
        ok = (x.min(1) > 1e-3) & (y.min(1) > 1e-3)
        d = ratios(x[ok]) - ratios(colour[None, :])[0]
        m = (h[ok][:, None] * d / (1 + (d / RATIO_SCALE) ** 2)).ravel()
        num += float(np.dot(m, (ratios(y[ok]) - ratios(x[ok])).ravel()))
        den += float(np.dot(m, m))
    return num / den if den > 0 else 0.0


def colour_law(samples, value):
    """(beta, kappa): airlight light when adding haze, saturation when removing it."""
    return (best_beta(samples), 0.0) if value < 0 else (0.0, best_kappa(samples))


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
    airlight_colours = {s: data[next(iter(data))][s][4].round(4).tolist() for s in data[next(iter(data))]}
    laws = {}
    for value, scenes in sorted(data.items()):
        parts = {s: normal_equations(v) for s, v in scenes.items()}
        lum = lum_den = col = col_kept = col_den = 0.0
        per_scene = {}
        for test in scenes:
            others = [s for s in scenes if s != test]
            table = solve([parts[s] for s in others])
            x, y, airlight, h, colour = scenes[test]
            lx, target = luminance_gain(table, bounds_of([scenes[s] for s in others]), x, airlight, h)
            beta, kappa = colour_law([scenes[s] for s in others], value)
            a, b = luminance_unexplained(saturate(recolour(x, lx, target, colour, beta), h, kappa, colour), x, y)
            c, d = colour_unexplained(scenes[test], beta, kappa)
            k, _ = colour_unexplained(scenes[test], 0.0)
            per_scene[test] = {"luminance": a / b, "colour": c / d, "colour_ratio_kept": k / d}
            lum, lum_den, col, col_kept, col_den = lum + a, lum_den + b, col + c, col_kept + k, col_den + d
        table = solve(list(parts.values()))
        bounds = bounds_of(list(scenes.values()))
        beta, kappa = colour_law(list(scenes.values()), value)
        laws[str(value)] = {"gain_ev": table.reshape(len(TONE_KNOTS), len(HAZE_KNOTS)).round(4).tolist(),
                            "gain_bounds_ev": [round(b, 4) for b in bounds], "beta": beta, "kappa": round(kappa, 4),
                            "leave_one_out_luminance_unexplained": lum / lum_den,
                            "leave_one_out_colour_unexplained": col / col_den,
                            "colour_unexplained_ratio_kept": col_kept / col_den,
                            "by_scene": per_scene, "scenes": sorted(scenes)}
        print(f"dehaze {value:+d}: luminance {lum / lum_den:.2f}; colour {col / col_den:.2f} (ratio kept "
              f"{col_kept / col_den:.2f}), beta {beta:.2f}, kappa {kappa:.2f}; bounds {bounds[0]:+.2f}/{bounds[1]:+.2f} EV", flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_dehaze_law.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "Y' = Y 2^f(log2(Y/A), h), f bilinear, clamped to gain_bounds_ev; colour: adding haze "
                       "(1 - beta) ratio-kept + beta airlight-coloured light, removing haze log ratios to green "
                       "from the airlight's, d, gain kappa h d / (1 + (d / R0)^2) at constant luminance",
              "ratio_scale_ev": RATIO_SCALE,
              "tone_knots_log2_of_airlight": TONE_KNOTS.tolist(), "haze_knots": HAZE_KNOTS.tolist(),
              "gain_ev_layout": "rows tone knots, columns haze knots", "smoothness": SMOOTHNESS,
              "reduction": REDUCTION, "haze_smoothing_native_px": 128, "dark_channel_window_native_px": 15,
              "airlight": "max(0.03, 99th percentile of the luminance)",
              "airlight_colour": f"mean RGB of the {HAZIEST:.1%} brightest eroded dark channel, luminance 1",
              "airlight_colours": airlight_colours, "sample_stride": STRIDE,
              "unexplained_metric": "luminance: share of Lightroom's luminance change in EV; colour: share of "
                                    "Lightroom's change of log2(R/G) and log2(B/G), with Lightroom's luminance",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "laws": laws}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
