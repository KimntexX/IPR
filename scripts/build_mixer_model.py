"""Generate src/core/ColorMixerModel.h: Lightroom colour mixer response tables.

For each Lightroom mixer reference of tests/image/base/color-mixer/<band> and
its neutral (base/neutre, XMP identical except the tested slider), pixels are
compared in Oklab: hue rotation (degrees), log chroma gain and log lightness
gain, binned by the neutral pixel's Oklab hue (5 degrees), linear ProPhoto
saturation and Oklab lightness. Every band, component (hue, saturation,
luminance) and slider value gets its own table; a cell is the pixel mean over
the scenes, falling back to (hue, saturation), then hue, then zero when sparse:
the hue is always kept, the effects being local in hue. The main quantity of
each component (rotation for Hue, chroma for Saturation, lightness for
Luminance) is made monotone in the slider value (isotonic fit on each side of
zero). Each band keeps only its hue window, whose two edge bins are zero so the
response fades to nothing outside it.

Magenta is re-centred by MAGENTA_SHIFT_BINS (user choice, 22 October 2026):
halfway between Lightroom's magenta and the band IPR used before.
A two-fold scene cross-validation scores the tables; scores and tables go to an
immutable evidence JSON.

Usage: python scripts/build_mixer_model.py --data-version N [--supersedes CAMPAIGN_ID]
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
sys.path.insert(0, str(ROOT / "scripts"))
BASE = ROOT / "tests/image/base"
BANDS = ("red", "orange", "yellow", "green", "cyan", "blue", "violet", "magenta")
LR_BANDS = ("Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta")
COMPONENTS = ("Hue", "Saturation", "Luminance")
VALUES = (-100, -50, -25, 25, 50, 100)
STEP = 8
HUE_STEP = 5
HUE_BINS = 360 // HUE_STEP
S_EDGES = (0, .1, .2, .35, .5, .7, 1.0)
L_EDGES = (0, .3, .45, .6, .75, 1.2)
MINIMUM = 40
NEAR_GREY = 0.02          # Oklab chroma below which pixels are not fitted and effects fade out
WINDOW_THRESHOLD = (0.3, 0.01, 0.01)  # rotation (deg), log chroma, log lightness
MAGENTA_SHIFT_BINS = -2   # -10 degrees: halfway towards the former IPR magenta (about -21 degrees)
MAIN = (0, 1, 2)          # main quantity of Hue, Saturation, Luminance


def read(path):
    from analysis.native import inspect
    return np.asarray(inspect(path)[0][::STEP, ::STEP, :3], np.float64).reshape(-1, 3) / 65535


def scene_stats(job):
    """[band, component, value, quantity, moment(n, sum), hue, s, l] for one scene."""
    import build_vibrance_model as m
    stem, files = job
    ne = read(BASE / "neutre" / f"{stem}.tif")
    n = m.linear(ne)
    lab = m.oklab(n)
    c = np.hypot(lab[:, 1], lab[:, 2])
    hue = np.degrees(np.arctan2(lab[:, 2], lab[:, 1])) % 360
    pp = n @ m.TO_PROPHOTO.T
    s = np.where(pp.max(1) > 1e-6, (pp.max(1) - pp.min(1)) / np.maximum(pp.max(1), 1e-6), 0)
    base_ok = ((ne > .002) & (ne < .995)).all(1) & (c > NEAR_GREY) & (lab[:, 0] > .08)
    ns, nl = len(S_EDGES) - 1, len(L_EDGES) - 1
    hb = np.minimum((hue / HUE_STEP).astype(int), HUE_BINS - 1)
    sb = np.clip(np.digitize(s, S_EDGES) - 1, 0, ns - 1)
    lb = np.clip(np.digitize(lab[:, 0], L_EDGES) - 1, 0, nl - 1)
    cell = (hb * ns + sb) * nl + lb
    ncell = HUE_BINS * ns * nl
    stats = np.zeros((len(BANDS), len(COMPONENTS), len(VALUES), 3, 2, ncell))
    for (bi, ci, vi), path in files.items():
        ve = read(path)
        if ve.shape != ne.shape:
            raise ValueError("Size differs from the neutral: " + str(path))
        ok = base_ok & ((ve > .002) & (ve < .995)).all(1)
        lv = m.oklab(m.linear(ve[ok]))
        rot = (np.degrees(np.arctan2(lv[:, 2], lv[:, 1])) - hue[ok] + 180) % 360 - 180
        quantities = (np.clip(rot, -90, 90),
                      np.clip(np.log(np.maximum(np.hypot(lv[:, 1], lv[:, 2]), 1e-5) / c[ok]), -3, 3),
                      np.clip(np.log(np.maximum(lv[:, 0], 1e-5) / lab[ok, 0]), -1, 1))
        k = cell[ok]
        for qi, q in enumerate(quantities):
            stats[bi, ci, vi, qi, 0] = np.bincount(k, minlength=ncell)
            stats[bi, ci, vi, qi, 1] = np.bincount(k, q, minlength=ncell)
    return stem, stats.reshape(stats.shape[:5] + (HUE_BINS, ns, nl)).astype(np.float32)


def fit(stats):
    """Mean per (hue, s, l) cell with hue-preserving fallbacks; stats summed over scenes."""
    n, total = stats[..., 0, :, :, :], stats[..., 1, :, :, :]

    def mean(n_, s_, fallback):
        return np.where(n_ >= MINIMUM, s_ / np.maximum(n_, 1), fallback)
    hue_only = mean(n.sum((-2, -1)), total.sum((-2, -1)), 0.0)[..., None]
    hue_s = mean(n.sum(-1), total.sum(-1), hue_only)[..., None]
    return mean(n, total, hue_s)  # [band, comp, value, quantity, hue, s, l]


def isotonic(values):
    blocks = []
    for v in values:
        blocks.append([v, 1])
        while len(blocks) > 1 and blocks[-2][0] > blocks[-1][0]:
            v2, n2 = blocks.pop()
            v1, n1 = blocks.pop()
            blocks.append([(v1 * n1 + v2 * n2) / (n1 + n2), n1 + n2])
    return [v for v, n in blocks for _ in range(n)]


def monotone(table):
    """Main quantity of each component non-decreasing in the value, per cell, zero fixed."""
    out = table.copy()
    negative = [i for i, v in enumerate(VALUES) if v < 0]
    positive = [i for i, v in enumerate(VALUES) if v > 0]
    adjusted = 0
    for b in range(len(BANDS)):
        for c in range(len(COMPONENTS)):
            q = MAIN[c]
            flat = out[b, c, :, q].reshape(len(VALUES), -1)
            before = flat.copy()
            for k in range(flat.shape[1]):
                flat[negative, k] = np.minimum(isotonic(flat[negative, k]), 0)
                flat[positive, k] = np.maximum(isotonic(flat[positive, k]), 0)
            out[b, c, :, q] = flat.reshape(out[b, c, :, q].shape)
            adjusted += int((np.abs(flat - before) > 1e-12).any(0).sum())
    return out, adjusted


def windows(table):
    """Per band: first hue bin and bin count of the contiguous hue window around its response."""
    result = []
    for b in range(len(BANDS)):
        strength = np.zeros(HUE_BINS, bool)
        for q in range(3):
            profile = np.abs(table[b, :, :, q]).max(axis=(0, 1, 3, 4))
            strength |= profile > WINDOW_THRESHOLD[q]
        peak = int(np.argmax(np.abs(table[b, 0, VALUES.index(100), 0]).max(axis=(1, 2))))
        if not strength[peak]:
            raise ValueError(f"No response around the {BANDS[b]} peak")
        lo = hi = peak
        while strength[(lo - 1) % HUE_BINS] and hi - lo < HUE_BINS - 4:
            lo -= 1
        while strength[(hi + 1) % HUE_BINS] and hi - lo < HUE_BINS - 4:
            hi += 1
        lo, hi = lo - 2, hi + 2  # one margin bin, one zero edge bin on each side
        result.append((lo % HUE_BINS, hi - lo + 1))
    return result


def windowed(table, wins, shift):
    """Band tables restricted to their windows, edges zeroed; magenta shifted by `shift` bins."""
    blocks, firsts = [], []
    for b, (first, count) in enumerate(wins):
        bins = [(first + i) % HUE_BINS for i in range(count)]
        block = table[b][:, :, :, bins].copy()        # [comp, value, quantity, bin, s, l]
        block[:, :, :, 0] = 0
        block[:, :, :, -1] = 0
        blocks.append(block)
        firsts.append((first + (shift if BANDS[b] == "magenta" else 0)) % HUE_BINS)
    return blocks, firsts


def predict(blocks, firsts, b, c, v, q, counts):
    """Pixel-weighted mean predicted quantity over the band window for one scene's cell counts."""
    block = blocks[b][c, v, q]
    count = block.shape[0]
    bins = [(firsts[b] + i) % HUE_BINS for i in range(count)]
    n = counts[bins]
    return float((block * n).sum() / max(n.sum(), 1)), n.sum()


def cross_validation(data, shift):
    scenes = sorted(data)
    folds = [scenes[i::2] for i in range(2)]
    errors = {q: [] for q in range(3)}
    per_state = []
    for fold in folds:
        train = sum(data[s].astype(np.float64) for s in scenes if s not in fold)
        table, _ = monotone(fit(train))
        blocks, firsts = windowed(table, windows(table), shift)
        for s in fold:
            st = data[s].astype(np.float64)
            for b in range(len(BANDS)):
                for c in range(len(COMPONENTS)):
                    for v in range(len(VALUES)):
                        q = MAIN[c]
                        count = blocks[b].shape[3]
                        bins = [(firsts[b] + i) % HUE_BINS for i in range(count)]
                        n = st[b, c, v, q, 0][bins]
                        if n.sum() < 500:
                            continue
                        actual = st[b, c, v, q, 1][bins].sum() / n.sum()
                        predicted = float((blocks[b][c, v, q] * n).sum() / n.sum())
                        errors[q].append(abs(predicted - actual))
                        per_state.append({"scene": s, "band": BANDS[b], "component": COMPONENTS[c],
                                          "value": VALUES[v], "quantity": ["rotation_degrees", "log_chroma",
                                                                           "log_lightness"][q],
                                          "lr": round(actual, 5), "predicted": round(predicted, 5)})
    summary = {name: {"median_abs": float(np.median(errors[q])), "p90_abs": float(np.percentile(errors[q], 90)),
                      "states": len(errors[q])}
               for q, name in enumerate(("rotation_degrees", "log_chroma", "log_lightness"))}
    return summary, per_state


def centres(edges):
    return [round((a + b) / 2, 4) for a, b in zip(edges[:-1], edges[1:])]


def header(blocks, firsts):
    offsets = np.cumsum([0] + [blk.shape[3] for blk in blocks[:-1]]).tolist()
    total = sum(blk.shape[3] for blk in blocks)
    # Layout: [bin][component][value][quantity][saturation][lightness]
    rows = []
    for blk in blocks:
        for i in range(blk.shape[3]):
            values = blk[:, :, :, i].reshape(-1)
            rows.append("    " + ", ".join(f"{x:.4g}" for x in values))
    ns, nl = len(S_EDGES) - 1, len(L_EDGES) - 1
    return f"""#ifndef IPR_CORE_COLORMIXERMODEL_H
#define IPR_CORE_COLORMIXERMODEL_H

// Generated by scripts/build_mixer_model.py from the Lightroom colour mixer
// references of tests/image/base (9 scenes), see the mixer report.
// Do not edit by hand: regenerate.

namespace ColorMixer::Model {{
inline constexpr int ValueCount = {len(VALUES)};
inline constexpr int Values[ValueCount] = {{{", ".join(str(v) for v in VALUES)}}};
inline constexpr int ComponentCount = {len(COMPONENTS)}; // hue, saturation, luminance
inline constexpr int QuantityCount = 3; // hue rotation (degrees), log chroma gain, log lightness gain
inline constexpr float HueStepDegrees = {HUE_STEP}.0f; // bin k centre: (k + 0.5) step, Oklab hue
inline constexpr int HueBinCount = {HUE_BINS};
inline constexpr int SaturationCount = {ns};
inline constexpr float Saturation[SaturationCount] = {{{", ".join(f"{x:.4f}f" for x in centres(S_EDGES))}}};
inline constexpr int LightnessCount = {nl};
inline constexpr float Lightness[LightnessCount] = {{{", ".join(f"{x:.4f}f" for x in centres(L_EDGES))}}};
// Oklab chroma below which the response fades out linearly (greys unchanged).
inline constexpr float NearGreyChroma = {NEAR_GREY}f;
// Hue window of each band (red, orange, yellow, green, cyan, blue, violet,
// magenta): first hue bin, bin count, offset of its first row in Response.
inline constexpr int BandFirstBin[8] = {{{", ".join(str(f) for f in firsts)}}};
inline constexpr int BandBinCount[8] = {{{", ".join(str(b.shape[3]) for b in blocks)}}};
inline constexpr int BandOffset[8] = {{{", ".join(str(o) for o in offsets)}}};
inline constexpr int RowCount = {total};
inline constexpr int RowSize = ComponentCount * ValueCount * QuantityCount * SaturationCount * LightnessCount;
// Response[row][component][value][quantity][saturation][lightness], flattened per row.
inline constexpr float Response[RowCount][RowSize] = {{
{("," + chr(10)).join("{" + r.strip() + "}" for r in rows)}
}};
}}

#endif // IPR_CORE_COLORMIXERMODEL_H
"""


def main():
    from analysis.native import embedded_metadata, sha256
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--header", type=Path, default=ROOT / "src/core/ColorMixerModel.h")
    parser.add_argument("--evidence-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--supersedes", help="Replaced campaign ID, without changing its JSON")
    args = parser.parse_args()
    jobs, seen = {}, {}
    for bi, (band, lr) in enumerate(zip(BANDS, LR_BANDS)):
        for path in sorted((BASE / "color-mixer" / band).iterdir()):
            if path.suffix.lower() not in (".tif", ".tiff"):
                continue
            x = embedded_metadata(path)[1]
            nonzero = [(ci, int(float(x[f"{comp}Adjustment{lr}"]))) for ci, comp in enumerate(COMPONENTS)
                       if float(x.get(f"{comp}Adjustment{lr}", 0)) != 0]
            if len(nonzero) != 1 or nonzero[0][1] not in VALUES:
                raise ValueError("Unexpected mixer state: " + str(path))
            ci, value = nonzero[0]
            key = (bi, ci, VALUES.index(value))
            stem = path.stem.split("-")[0]
            if key in jobs.setdefault(stem, {}):
                raise ValueError(f"Duplicated state {key} for {stem}: {path}")
            jobs[stem][key] = path
            seen.setdefault(stem, {})[f"{band}/{COMPONENTS[ci].lower()}/{value}"] = path
    expected = len(BANDS) * len(COMPONENTS) * len(VALUES)
    for stem, files in jobs.items():
        if len(files) != expected:
            raise ValueError(f"Incomplete mixer states for {stem}: {len(files)}/{expected}")
    with ProcessPoolExecutor(4) as pool:
        data = dict(pool.map(scene_stats, sorted(jobs.items())))
    table, adjusted = monotone(fit(sum(d.astype(np.float64) for d in data.values())))
    wins = windows(table)
    blocks, firsts = windowed(table, wins, MAGENTA_SHIFT_BINS)
    validation, per_state = cross_validation(data, 0)
    validation_shifted, _ = cross_validation(data, MAGENTA_SHIFT_BINS)
    stamp = datetime.now(timezone.utc)
    campaign = f"modele-melangeur-lr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%S%fZ')}"
    evidence = args.evidence_dir / f"{campaign}.json"
    if evidence.exists():
        sys.exit("Exists, nothing overwritten: " + str(evidence))
    args.header.write_text(header(blocks, firsts), encoding="utf-8", newline="\n")
    record = {
        "campaign_id": campaign, "measured_at_utc": stamp.isoformat(), "supersedes_campaign_id": args.supersedes,
        "purpose": "Lightroom colour mixer response tables of the production model (ColorMixerModel.h)",
        "generator": "scripts/build_mixer_model.py",
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "method": {"step": STEP, "cell_minimum": MINIMUM, "near_grey_chroma": NEAR_GREY,
                   "statistic": "pixel mean per cell over the scenes (rotation clipped to +-90 deg, log gains to +-3/+-1)",
                   "excluded": "encoded channel <= 0.002 or >= 0.995 in either export; Oklab C <= near grey; L <= 0.08",
                   "axes": {"hue_step_degrees": HUE_STEP, "saturation_edges": S_EDGES, "lightness_edges": L_EDGES},
                   "fallback": "(hue, s, l) -> (hue, s) -> hue -> 0",
                   "monotone": "isotonic per cell of the main quantity on each side of zero",
                   "monotone_adjusted_cells": adjusted,
                   "window_threshold": WINDOW_THRESHOLD,
                   "magenta_shift_bins": MAGENTA_SHIFT_BINS,
                   "magenta_shift_reason": "user choice 2026-10-22: keep magenta near the former IPR band, shifted slightly",
                   "cross_validation": "two folds of alternate scenes; error = |predicted - LR| pixel mean over the band window, main quantity"},
        "inputs": {stem: {"neutral": {"file": f"neutre/{stem}.tif", "sha256": sha256(BASE / "neutre" / f"{stem}.tif")},
                          "variants": {k: {"file": str(p.relative_to(BASE)).replace("\\", "/"), "sha256": sha256(p)}
                                       for k, p in sorted(seen[stem].items())}}
                   for stem in sorted(jobs)},
        "header_sha256": hashlib.sha256(args.header.read_bytes()).hexdigest(),
        "windows": {band: {"first_bin": f, "bins": blk.shape[3]} for band, f, blk in zip(BANDS, firsts, blocks)},
        "tables": {"values": VALUES, "bands": BANDS, "components": COMPONENTS,
                   "layout": "per band: [component][value][quantity][bin][saturation][lightness]",
                   "per_band": {band: blk.round(5).tolist() for band, blk in zip(BANDS, blocks)}},
        "cross_validation": {"lightroom_centres": validation, "with_magenta_shift": validation_shifted,
                             "per_state": per_state},
    }
    evidence.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for name, s in validation.items():
        print(f"validation croisée {name}: médiane {s['median_abs']:.4f}, p90 {s['p90_abs']:.4f} ({s['states']} états)")
    print("fenêtres:", {b: (f * HUE_STEP, blk.shape[3] * HUE_STEP) for b, f, blk in zip(BANDS, firsts, blocks)})
    print(f"{args.header} ({args.header.stat().st_size // 1024} Kio)\n{evidence}")


if __name__ == "__main__":
    main()
