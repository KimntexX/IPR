"""Fit the Shadows/Blacks/Highlights/Whites model (plan B) on Lightroom's own
transfer curves and generate src/core/ToneRecoveryModel.h.

Inputs: the Lumieres LR references of tests/image/base (lumiere/<control>/ and
their LR neutral in neutre/, matched by XMP), pixel-aligned since both come from
the same RAW. Per control and sign: EV lift shapes (knots every EV, four slider
levels) and, for Shadows, Highlights and positive Whites, a per-image gain
predicted from luminance statistics. Leave-one-scene-out errors are printed.

Usage: python scripts/fit_tone_recovery.py [--json OUT.json] [--header OUT.h]
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from scipy.optimize import least_squares

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis.audit import FIELDS, finite_number  # noqa: E402
from analysis.native import embedded_metadata, encoded, inspect, linearize  # noqa: E402

BASE = ROOT / "tests/image/base"
LUMA = np.array([.2126, .7152, .0722])
STEP = 8
IGNORED = {"HasSettings", "Version", "UUID", "Digest"}
KNOTS = np.arange(-16, 2.01, 1.0)
EDGES = np.arange(-14, 0.001, .25)
MID = (EDGES[1:] + EDGES[:-1]) / 2
LEVELS = (25, 50, 75, 100)
PER_CHANNEL = {"blacks", "whites"}
# Model::Statistic order: mean, then quantiles 1, 5, 10, 90, 95, 99.9 %.
STATISTICS = {"Mean": None, "Q1": .01, "Q5": .05, "Q10": .10, "Q90": .90, "Q95": .95, "Q999": .999}
# Statistic pairs chosen by leave-one-out prediction of the per-scene gains.
GAIN_STATISTICS = {("shadows", -1): ("Q90", "Q95"), ("shadows", 1): ("Q10", "Q95"),
                   ("highlights", -1): ("Q1", "Mean"), ("highlights", 1): ("Q1", "Mean"),
                   ("whites", 1): ("Q95", "Q999")}
ORDER = [(f, s) for f in ("shadows", "blacks", "highlights", "whites") for s in (-1, 1)]


SHOULDER_KNEE = 0.70


def predict(x, lift, feature):
    """log2 output for log2 input x and EV lift. Per-channel brightening rolls
    off near white through the soft shoulder of ToneRecovery::shoulder, as
    Lightroom does per channel; luminance controls are capped at white."""
    if feature not in PER_CHANNEL:
        return np.minimum(x + lift, 0)
    y, out = 2.0 ** x, 2.0 ** (x + lift)
    base = np.maximum(y, SHOULDER_KNEE)
    soft = base + (1 - base) * np.tanh((out - base) / np.maximum(1 - base, 1e-6))
    out = np.where((out > y) & (out > SHOULDER_KNEE), soft, np.minimum(out, 1.0))
    return np.log2(np.maximum(out, 2.0 ** -40))


def lightroom(path):
    ipr, xmp = embedded_metadata(path)
    return None if ipr or not xmp else xmp


def pixels(path):
    data, _, _ = inspect(path)
    return linearize(encoded(data, step=STEP)).reshape(-1, 3)


def load_pairs():
    neutrals, pairs = {}, {}
    for p in sorted((BASE / "neutre").iterdir()):
        if p.suffix.lower() in (".tif", ".tiff") and (xmp := lightroom(p)):
            neutrals.setdefault(p.stem.split("-")[0], []).append((p, xmp))
    for feature in ("shadows", "blacks", "highlights", "whites"):
        field = FIELDS[feature]
        for p in sorted((BASE / "lumiere" / feature).iterdir()):
            if p.suffix.lower() not in (".tif", ".tiff") or not (xmp := lightroom(p)):
                continue
            scene = p.name.split("-")[0].split(".")[0]
            match = [n for n, nx in neutrals.get(scene, [])
                     if all(xmp.get(k) == nx.get(k) for k in (xmp.keys() | nx.keys()) - IGNORED - {field})]
            if not match:
                print("sans neutre LR :", p.name)
                continue
            if ("neutral", scene) not in pairs:
                pairs[("neutral", scene)] = pixels(match[0])
            pairs[(feature, scene, finite_number(xmp.get(field)))] = pixels(p)
    return pairs


def statistics(rgb):
    x = np.log2(np.maximum(rgb @ LUMA, 2 ** -20))
    return {k: float(x.mean() if q is None else np.quantile(x, q)) for k, q in STATISTICS.items()}


def binned(pairs, scene, feature, value):
    n, v = pairs[("neutral", scene)], pairs[(feature, scene, value)]
    a, b = (n.ravel(), v.ravel()) if feature in PER_CHANNEL else (n @ LUMA, v @ LUMA)
    keep = (a > 2 ** -14) & (a < .98)
    x, y = np.log2(a[keep]), np.log2(np.maximum(b[keep], 2 ** -16))
    idx = np.digitize(x, EDGES) - 1
    med, cnt = np.full(len(MID), np.nan), np.zeros(len(MID))
    for i in range(len(MID)):
        m = idx == i
        if m.sum() > 100:
            med[i], cnt[i] = np.median(y[m]), m.sum()
    return med, cnt / max(cnt.sum(), 1)


def fit_control(pairs, stats, feature, sign, train):
    data = {(s, level): binned(pairs, s, feature, sign * level) for s in train for level in LEVELS}
    names = GAIN_STATISTICS.get((feature, sign))
    nk = len(KNOTS)
    X = {s: np.array([1.0] + [stats[s][k] for k in names]) if names else None for s in train}

    def unpack(p):
        return p[:4 * nk].reshape(4, nk), (p[4 * nk:] if names else None)

    def log_gain(coef, s):
        return 0.0 if coef is None else float(X[s] @ coef)

    def residuals(p):
        shapes, coef = unpack(p)
        out = []
        for (s, level), (med, w) in data.items():
            ok = ~np.isnan(med)
            pred = predict(MID[ok], np.exp(log_gain(coef, s)) *
                           np.interp(MID[ok], KNOTS, shapes[LEVELS.index(level)]), feature)
            out.append(np.sqrt(w[ok]) * (pred - med[ok]))
        out.append(.02 * np.diff(shapes, 2, axis=1).ravel())  # smooth shapes
        if names:  # gains centred on 1
            out.append(np.array([.3 * np.mean([log_gain(coef, s) for s in train])]))
        return np.concatenate(out)

    p0 = np.zeros(4 * nk + (len(names) + 1 if names else 0))
    shapes, coef = unpack(least_squares(residuals, p0, loss="soft_l1", f_scale=.1, max_nfev=4000).x)
    gains = [log_gain(coef, s) for s in train]
    return shapes, coef, ((min(gains) - .3, max(gains) + .3) if names else (0.0, 0.0))


def error(pairs, stats, feature, sign, model, scene):
    shapes, coef, (lo, hi) = model
    names = GAIN_STATISTICS.get((feature, sign))
    lg = 0.0 if coef is None else float(np.clip(np.array([1.0] + [stats[scene][k] for k in names]) @ coef, lo, hi))
    errs = []
    for level in LEVELS:
        med, w = binned(pairs, scene, feature, sign * level)
        ok = ~np.isnan(med)
        pred = predict(MID[ok], np.exp(lg) * np.interp(MID[ok], KNOTS, shapes[LEVELS.index(level)]), feature)
        errs.append(np.sum(w[ok] * np.abs(pred - med[ok])) / w[ok].sum())
    return np.array(errs)


def header(models, stats):
    def f(v):
        return f"{v:.4f}f"
    lines = ["#ifndef IPR_TONE_RECOVERY_MODEL_H", "#define IPR_TONE_RECOVERY_MODEL_H", "",
             "// Generated by scripts/fit_tone_recovery.py from Lightroom's own transfer curves",
             "// (LR neutral -> LR variant, pixel-aligned) of the eight Lumières scenes,",
             "// 18 October 2026. Do not edit by hand: refit and regenerate.", "",
             "namespace ToneRecovery::Model {",
             f"inline constexpr float FirstKnotEv = {f(KNOTS[0])};", f"inline constexpr int KnotCount = {len(KNOTS)};",
             "inline constexpr float Levels[4] = {" + ", ".join(f(level / 100) for level in LEVELS) + "};",
             "// Image statistics, log2 of linear luminance: mean, then quantiles 1, 5, 10, 90, 95, 99.9 %.",
             "enum Statistic { " + ", ".join(STATISTICS) + ", StatisticCount };",
             "struct Control {", "    bool perChannel;",
             "    float lift[4][KnotCount]; // EV lift per level and knot, before the image gain",
             "    int gainStatistics[2]; int gainStatisticCount;",
             "    float gainCoefficients[3]; // log gain = c0 + c1 s1 + c2 s2",
             "    float logGainMin, logGainMax;", "};",
             "// Order: Shadows -, +; Blacks -, +; Highlights -, +; Whites -, +.",
             "inline constexpr Control Controls[8] = {"]
    for feature, sign in ORDER:
        shapes, coef, (lo, hi) = models[(feature, sign)]
        shapes = np.array(shapes)
        for s in shapes:  # no data below 2^-14: lifts fade to 0 at 2^-16 (noise floor stays dark)
            if s[2] > 0:
                s[1], s[0] = s[2] / 2, 0.0
            else:
                s[1], s[0] = min(s[1], 0), min(s[0], 0)
        names = list(GAIN_STATISTICS.get((feature, sign), ()))
        co = [0.0, 0.0, 0.0] if coef is None else list(coef)
        lines.append(f"    // {feature}{'+' if sign > 0 else '-'}")
        lines.append("    {" + ("true" if feature in PER_CHANNEL else "false") + ", {")
        lines += ["        {" + ", ".join(f(v) for v in s) + "}," for s in shapes]
        lines.append(f"    }}, {{{', '.join(names + ['Mean'] * (2 - len(names)))}}}, {len(names)}, "
                     f"{{{', '.join(f(v) for v in co)}}}, {f(lo)}, {f(hi)}}},")
    typical = [float(np.median([s[k] for s in stats.values()])) for k in STATISTICS]
    lines += ["};", "", "// Median statistics of the eight scenes, for images without measurement.",
              "inline constexpr float TypicalStatistics[StatisticCount] = {" + ", ".join(f(v) for v in typical) + "};",
              "}", "#endif", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json", type=Path, help="also write the fitted model as JSON")
    parser.add_argument("--header", type=Path, default=ROOT / "src/core/ToneRecoveryModel.h")
    args = parser.parse_args()
    pairs = load_pairs()
    scenes = sorted(k[1] for k in pairs if k[0] == "neutral")
    stats = {s: statistics(pairs[("neutral", s)]) for s in scenes}
    models = {}
    for feature, sign in ORDER:
        model = fit_control(pairs, stats, feature, sign, scenes)
        fitted = np.array([error(pairs, stats, feature, sign, model, s) for s in scenes])
        loo = np.array([error(pairs, stats, feature, sign,
                              fit_control(pairs, stats, feature, sign, [t for t in scenes if t != s]), s)
                        for s in scenes])
        print(f"{feature:10s} {'+' if sign > 0 else '-'}  ajusté {fitted.mean():.3f} EV (±100 : {fitted[:, 3].mean():.3f})"
              f"  validation croisée {loo.mean():.3f} EV (±100 : {loo[:, 3].mean():.3f}, pire {loo[:, 3].max():.3f})",
              flush=True)
        models[(feature, sign)] = model
    args.header.write_text(header(models, stats), encoding="utf-8", newline="\n")
    if args.json:
        args.json.write_text(json.dumps({
            "scenes": stats, "controls": {f"{f}{'+' if s > 0 else '-'}": {
                "shapes": np.asarray(m[0]).tolist(), "gainCoefficients": [] if m[1] is None else list(m[1]),
                "logGainRange": list(m[2])} for (f, s), m in models.items()}}, indent=1), encoding="utf-8")
    print("écrit :", args.header)


if __name__ == "__main__":
    main()
