"""Generate src/core/VibranceModel.h: Lightroom vibrance response tables.

For each Lightroom vibrance reference of tests/image/base/couleur/vibrance and
its neutral (base/neutre, XMP identical except Vibrance), pixels are compared
in Oklab: log chroma gain, log lightness gain and hue rotation per cell of the neutral
pixel's linear ProPhoto saturation, Oklab lightness and Oklab hue. Cells take
the median over the 49 scenes, falling back to (saturation, lightness) then
saturation when sparse. Pixels clipped in either export are excluded.
Each cell's chroma gain is made monotone in the slider value (isotonic fit on
each side of zero, zero fixed): a stronger slider never gives less chroma.
Hue rotation falls back along the hue (never pooled across hues).
Near-greys (Oklab C <= 0.01, outside the tables' fit) get a factor on the log
chroma gain per chroma bin and measured value, least squares against LR.
A two-fold scene cross-validation compares the table with the model it
replaces; its scores and the tables go to an immutable evidence JSON.

Usage: python scripts/build_vibrance_model.py --data-version N [--supersedes CAMPAIGN_ID]
           [--evidence-dir docs/validation/raw]
"""
import argparse
import hashlib
import json
import sys
from concurrent.futures import ProcessPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
BASE = ROOT / "tests/image/base"
STEP = 8
SAMPLES = 60000
FIT_CHROMA = 0.01
NEAR_GREY_EDGES = (0.001, .002, .004, .007, .010, .013, .016)
NEAR_GREY_ANCHOR = 0.02  # factor 1 from here
MINIMUM = 40
VALUES = (-100, -50, -25, 25, 50, 100)
S_EDGES = (0, .05, .1, .15, .2, .3, .4, .5, .6, .7, .8, .9, 1.0)
L_EDGES = (0, .2, .3, .4, .5, .6, .7, .8, .9, 1.2)
HUE_BINS = 12

M1 = np.array([[.4122214708, .5363325363, .0514459929], [.2119034982, .6806995451, .1073969566],
               [.0883024619, .2817188376, .6299787005]])
M2 = np.array([[.2104542553, .7936177850, -.0040720468], [1.9779984951, -2.4285922050, .4505937099],
               [.0259040371, .7827717662, -.8086757660]])
# Linear sRGB (D65) -> linear ProPhoto (D50, Bradford).
SRGB_XYZ = np.array([[.4123907993, .3575843394, .1804807884], [.2126390059, .7151686788, .0721923154],
                     [.0193308187, .1191947798, .9505321522]])
BRADFORD = np.array([[1.0478112, .0228866, -.0501270], [.0295424, .9904844, -.0170491],
                     [-.0092345, .0150436, .7521316]])
PROPHOTO_XYZ = np.array([[.7976749, .1351917, .0313534], [.2880402, .7118741, .0000857], [0, 0, .8252100]])
TO_PROPHOTO = np.linalg.inv(PROPHOTO_XYZ) @ BRADFORD @ SRGB_XYZ


def read(path):
    from analysis.native import inspect
    return np.asarray(inspect(path)[0][::STEP, ::STEP, :3], dtype=np.float64).reshape(-1, 3) / 65535.0


def linear(encoded):
    return np.where(encoded <= 0.04045, encoded / 12.92, ((encoded + 0.055) / 1.055) ** 2.4)


def oklab(rgb):
    return np.cbrt(rgb @ M1.T) @ M2.T


def samples(job):
    """Rows: value, L, C, hue (degrees), ProPhoto saturation, L variant, C variant,
    hue rotation (degrees, variant - neutral)."""
    stem, files = job
    neutral_encoded = read(BASE / "neutre" / f"{stem}.tif")
    neutral = linear(neutral_encoded)
    lab = oklab(neutral)
    prophoto = neutral @ TO_PROPHOTO.T
    peak = prophoto.max(1)
    saturation = np.where(peak > 1e-6, (peak - prophoto.min(1)) / np.maximum(peak, 1e-6), 0)
    chroma = np.hypot(lab[:, 1], lab[:, 2])
    rows = []
    for value, path in files:
        variant_encoded = read(path)
        if variant_encoded.shape != neutral_encoded.shape:
            raise ValueError("Size differs from the neutral: " + str(path))
        keep = ((neutral_encoded > 0.002) & (neutral_encoded < 0.995) &
                (variant_encoded > 0.002) & (variant_encoded < 0.995)).all(1)
        keep &= (chroma > NEAR_GREY_EDGES[0]) & (lab[:, 0] > 0.08)
        index = np.flatnonzero(keep)
        if len(index) > SAMPLES:
            seed = int.from_bytes(hashlib.sha256(f"{stem}{value}".encode()).digest()[:4], "little")
            index = np.random.default_rng(seed).choice(index, SAMPLES, replace=False)
        variant = oklab(linear(variant_encoded[index]))
        hue = np.degrees(np.arctan2(lab[index, 2], lab[index, 1])) % 360
        rotation = (np.degrees(np.arctan2(variant[:, 2], variant[:, 1])) - hue + 180) % 360 - 180
        rows.append(np.column_stack([np.full(len(index), value), lab[index, 0], chroma[index], hue,
                                     saturation[index], variant[:, 0], np.hypot(variant[:, 1], variant[:, 2]),
                                     rotation]))
    return stem, np.vstack(rows)


def cells(rows):
    s = np.clip(np.digitize(rows[:, 4], S_EDGES) - 1, 0, len(S_EDGES) - 2)
    l = np.clip(np.digitize(rows[:, 1], L_EDGES) - 1, 0, len(L_EDGES) - 2)
    h = np.minimum((rows[:, 3] // (360 / HUE_BINS)).astype(int), HUE_BINS - 1)
    return s, l, h


def fit(rows, column):
    """Median per (saturation, lightness, hue) cell; column 6: log chroma gain,
    5: log lightness gain, 7: hue rotation in degrees. Sparse cells fall back to
    (saturation, lightness) then saturation for the gains, and to (saturation,
    hue), hue, then zero for the rotation, which depends on the hue first."""
    s, l, h = cells(rows)
    if column == 7:
        y = rows[:, 7]
    else:
        reference = rows[:, 2] if column == 6 else rows[:, 1]
        y = np.log(np.maximum(rows[:, column], 1e-5) / reference)
    ns, nl = len(S_EDGES) - 1, len(L_EDGES) - 1
    median = lambda m, fallback: np.median(y[m]) if m.sum() >= MINIMUM else fallback
    table = np.zeros((ns, nl, HUE_BINS))
    if column == 7:
        for k in range(HUE_BINS):
            hue = median(h == k, 0.0)
            for i in range(ns):
                level = median((s == i) & (h == k), hue)
                for j in range(nl):
                    table[i, j, k] = median((s == i) & (l == j) & (h == k), level)
        return table
    for i in range(ns):
        base = np.median(y[s == i]) if (s == i).any() else 0.0
        for j in range(nl):
            m = (s == i) & (l == j)
            middle = median(m, base)
            for k in range(HUE_BINS):
                table[i, j, k] = median(m & (h == k), middle)
    return table


def isotonic(values):
    """Pool-adjacent-violators: closest non-decreasing sequence (least squares)."""
    blocks = []
    for v in values:
        blocks.append([v, 1])
        while len(blocks) > 1 and blocks[-2][0] > blocks[-1][0]:
            v2, n2 = blocks.pop()
            v1, n1 = blocks.pop()
            blocks.append([(v1 * n1 + v2 * n2) / (n1 + n2), n1 + n2])
    return [v for v, n in blocks for _ in range(n)]


def monotone(tables):
    """Tables [value, ...] in VALUES order: non-decreasing in the value per cell,
    negative values <= 0 and positive values >= 0. Returns tables, adjusted cell count."""
    negative = [i for i, v in enumerate(VALUES) if v < 0]
    positive = [i for i, v in enumerate(VALUES) if v > 0]
    out = tables.copy()
    flat = out.reshape(len(VALUES), -1)
    for c in range(flat.shape[1]):
        flat[negative, c] = np.minimum(isotonic(flat[negative, c]), 0)
        flat[positive, c] = np.maximum(isotonic(flat[positive, c]), 0)
    return out, int((np.abs(out - tables) > 1e-12).reshape(len(VALUES), -1).any(0).sum())


def replaced_model(rows, value):
    """Chroma gain of the A1/A2 prototype this table replaces (baseline of the evidence)."""
    v = value / 100
    L, C, hue = rows[:, 1], rows[:, 2], rows[:, 3]

    def smooth(a, b, x):
        t = np.clip((x - a) / (b - a), 0, 1)
        return t * t * (3 - 2 * t)
    effective = v * (1 + (-.035 if v < 0 else .065) * smooth(.75, 1, abs(v)))
    relative = np.clip(C / np.maximum(np.abs(L), .05), 0, 1)
    distance = (hue - 50 + 180) % 360 - 180
    warm = 1 - .4 * np.exp(-(distance / 25) ** 2) * smooth(.10, .35, L) * (1 - smooth(.85, 1.10, L)) * smooth(.005, .025, C)
    weight = (1 - relative) ** 2 * warm if v >= 0 else 1 - .1 * relative
    return np.maximum(0, 1 + effective * weight)


def scene_gaps(rows, gain):
    """Predicted minus LR mean chroma change, pixels with s < 0.3 and s > 0.5."""
    gaps = []
    for low, high in ((0, .3), (.5, 1.01)):
        m = (rows[:, 4] >= low) & (rows[:, 4] < high)
        gaps.append(float(np.mean(gain[m]) - np.mean(rows[m, 6] / rows[m, 2])) if m.sum() >= 500 else None)
    return gaps


def cross_validation(data):
    scenes = sorted(data)
    folds = [scenes[i::2] for i in range(2)]
    tables = []
    for fold in folds:
        train = {s: d for s, d in data.items() if s not in fold}
        tables.append(monotone(np.array([fit(np.vstack([d[d[:, 0] == v] for d in train.values()]), 6)
                                         for v in VALUES]))[0])
    result = {}
    for vi, value in enumerate(VALUES):
        per_scene = {}
        for fold, fold_tables in zip(folds, tables):
            table = fold_tables[vi]
            for s in fold:
                rows = data[s][data[s][:, 0] == value]
                si, li, hi = cells(rows)
                per_scene[s] = {"table": scene_gaps(rows, np.exp(table[si, li, hi])),
                                "replaced_a1_a2": scene_gaps(rows, replaced_model(rows, value))}
        summary = {}
        for model in ("table", "replaced_a1_a2"):
            for index, group in enumerate(("low_saturation", "high_saturation")):
                gaps = np.abs([g[model][index] for g in per_scene.values() if g[model][index] is not None])
                summary[f"{model}_{group}"] = {"median_abs": float(np.median(gaps)), "p90_abs": float(np.percentile(gaps, 90)),
                                              "scenes": int(len(gaps))}
        result[str(value)] = {"summary": summary, "scenes": per_scene}
    return result


def centres(edges):
    return [round((a + b) / 2, 4) for a, b in zip(edges[:-1], edges[1:])]


def near_grey(data, chroma):
    """Factor on the table's log chroma gain per measured value and near-grey chroma bin."""
    factors = []
    for vi, value in enumerate(VALUES):
        rows = np.vstack([d[(d[:, 0] == value) & (d[:, 2] <= NEAR_GREY_EDGES[-1])] for d in data.values()])
        si, li, hi = cells(rows)
        pred = chroma[vi][si, li, hi]
        lr = np.log(np.maximum(rows[:, 6], 1e-5) / rows[:, 2])
        c = rows[:, 2]
        row = []
        for a, b in zip(NEAR_GREY_EDGES[:-1], NEAR_GREY_EDGES[1:]):
            k = (c > a) & (c <= b)
            row.append(float(np.clip(np.dot(lr[k], pred[k]) / max(np.dot(pred[k], pred[k]), 1e-12), 0, 1.5)))
        factors.append(row + [1.0])
    return np.array(factors)


def header(chroma, lightness, rotation, grey):
    def table(t):
        def nested(a, indent):
            if a.ndim == 1:
                return "{" + ", ".join(f"{x:.4f}f" for x in a) + "}"
            pad = "    " * indent
            return "{\n" + ",\n".join(pad + "    " + nested(b, indent + 1) for b in a) + "\n" + pad + "}"
        return nested(np.asarray(t), 0)
    s, l = centres(S_EDGES), centres(L_EDGES)
    return f"""#ifndef IPR_CORE_VIBRANCEMODEL_H
#define IPR_CORE_VIBRANCEMODEL_H

// Generated by scripts/build_vibrance_model.py from the Lightroom vibrance
// references of tests/image/base (49 scenes), see the Couleur report.
// Do not edit by hand: regenerate.

namespace Vibrance::Model {{
inline constexpr int ValueCount = {len(VALUES)};
inline constexpr int Values[ValueCount] = {{{", ".join(str(v) for v in VALUES)}}};
// Cell centres: linear ProPhoto saturation, Oklab lightness, Oklab hue.
inline constexpr int SaturationCount = {len(s)};
inline constexpr float Saturation[SaturationCount] = {{{", ".join(f"{x:.4f}f" for x in s)}}};
inline constexpr int LightnessCount = {len(l)};
inline constexpr float Lightness[LightnessCount] = {{{", ".join(f"{x:.4f}f" for x in l)}}};
inline constexpr int HueCount = {HUE_BINS};
inline constexpr float HueStepDegrees = {360 / HUE_BINS:.4f}f; // centre k: (k + 0.5) step
// Natural log of the Oklab chroma and lightness gains, [value][saturation][lightness][hue].
inline constexpr float LogChromaGain[ValueCount][SaturationCount][LightnessCount][HueCount] = {table(chroma)};
inline constexpr float LogLightnessGain[ValueCount][SaturationCount][LightnessCount][HueCount] = {table(lightness)};
// Oklab hue rotation in degrees, same axes.
inline constexpr float HueRotation[ValueCount][SaturationCount][LightnessCount][HueCount] = {table(rotation)};
// Factor on the log chroma gain by the pixel's Oklab chroma, [value][chroma];
// 1 from the last centre (near-grey protection, outside the tables' fit).
inline constexpr int NearGreyCount = {len(grey[0])};
inline constexpr float NearGreyChroma[NearGreyCount] = {{{", ".join(f"{x:.4f}f" for x in centres(NEAR_GREY_EDGES) + [NEAR_GREY_ANCHOR])}}};
inline constexpr float NearGreyFactor[ValueCount][NearGreyCount] = {table(grey)};
}}

#endif // IPR_CORE_VIBRANCEMODEL_H
"""


def main():
    from analysis.native import embedded_metadata, sha256
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--header", type=Path, default=ROOT / "src/core/VibranceModel.h")
    parser.add_argument("--evidence-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--supersedes", help="Replaced campaign ID, without changing its JSON")
    args = parser.parse_args()
    jobs = {}
    for path in sorted((BASE / "couleur/vibrance").iterdir()):
        if path.suffix.lower() not in (".tif", ".tiff"):
            continue
        value = int(float(embedded_metadata(path)[1]["Vibrance"]))
        if value not in VALUES:
            raise ValueError(f"Unexpected vibrance {value}: {path}")
        jobs.setdefault(path.stem.split("-")[0], []).append((value, path))
    for stem, files in jobs.items():
        if sorted(v for v, _ in files) != sorted(VALUES):
            raise ValueError(f"Incomplete or duplicated vibrance states for {stem}")
    with ProcessPoolExecutor(4) as pool:
        data = dict(pool.map(samples, sorted(jobs.items())))
    fitted = {s: d[d[:, 2] > FIT_CHROMA] for s, d in data.items()}
    chroma, adjusted = monotone(np.array([fit(np.vstack([d[d[:, 0] == v] for d in fitted.values()]), 6) for v in VALUES]))
    lightness = np.array([fit(np.vstack([d[d[:, 0] == v] for d in fitted.values()]), 5) for v in VALUES])
    rotation = np.array([fit(np.vstack([d[d[:, 0] == v] for d in fitted.values()]), 7) for v in VALUES])
    grey = near_grey(data, chroma)
    validation = cross_validation(fitted)
    stamp = datetime.now(timezone.utc)
    campaign = f"modele-vibrance-lr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%S%fZ')}"
    evidence = args.evidence_dir / f"{campaign}.json"
    if evidence.exists():
        sys.exit("Exists, nothing overwritten: " + str(evidence))
    args.header.write_text(header(chroma, lightness, rotation, grey), encoding="utf-8", newline="\n")
    record = {
        "campaign_id": campaign, "measured_at_utc": stamp.isoformat(), "supersedes_campaign_id": args.supersedes,
        "purpose": "Lightroom vibrance response tables of the production model (VibranceModel.h)",
        "generator": "scripts/build_vibrance_model.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "method": {"step": STEP, "samples_per_state": SAMPLES, "cell_minimum": MINIMUM,
                   "excluded": "encoded channel <= 0.002 or >= 0.995 in either export; L <= 0.08; tables: Oklab C <= 0.01; near-grey factor: 0.001 < C <= 0.016",
                   "axes": {"saturation_edges": S_EDGES, "lightness_edges": L_EDGES, "hue_bins": HUE_BINS},
                   "monotone": "isotonic per cell on each side of zero (chroma)", "monotone_adjusted_cells": adjusted,
                   "cross_validation": "two folds of alternate scenes; gap = predicted - LR mean chroma ratio"},
        "inputs": {stem: {"neutral": {"file": f"neutre/{stem}.tif", "sha256": sha256(BASE / "neutre" / f"{stem}.tif")},
                          "variants": {str(v): {"file": str(p.relative_to(BASE)).replace("\\", "/"), "sha256": sha256(p)}
                                       for v, p in sorted(files)}}
                   for stem, files in sorted(jobs.items())},
        "header_sha256": hashlib.sha256(args.header.read_bytes()).hexdigest(),
        "tables": {"values": VALUES, "near_grey_chroma_edges": NEAR_GREY_EDGES, "near_grey_anchor": NEAR_GREY_ANCHOR,
                   "near_grey_factor": grey.round(5).tolist(),
                   "log_chroma_gain": chroma.round(5).tolist(),
                   "log_lightness_gain": lightness.round(5).tolist(),
                   "hue_rotation_degrees": rotation.round(4).tolist()},
        "cross_validation": validation,
    }
    evidence.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for value in VALUES:
        s = validation[str(value)]["summary"]
        print(f"{value:+4d} table {s['table_low_saturation']['median_abs']:.3f}/{s['table_high_saturation']['median_abs']:.3f}"
              f"  A1/A2 {s['replaced_a1_a2_low_saturation']['median_abs']:.3f}/{s['replaced_a1_a2_high_saturation']['median_abs']:.3f}")
    print("near-grey factor per value:", grey.round(3).tolist())
    print(f"{args.header}\n{evidence}")


if __name__ == "__main__":
    main()
