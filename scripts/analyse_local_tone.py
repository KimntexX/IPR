"""What drives Lightroom's Highlights control beyond the pixel luminance?
Diagnostic before a LocalTone module.

On the Lightroom pairs neutral -> Highlights variant of tests/image/base
(pixel-aligned, same RAW), the EV change of each pixel, e = log2(Y1 / Y0),
is explained by a predictor of the neutral pixel: its luminance Y (the current
global curve), another brightness (max channel, channel mean), a smoothed base
of its neighbourhood, or a two-dimensional table (Y and saturation, hue,
neighbourhood base or local detail).

Two tests per predictor:
  - explanatory: per scene, RMS of e around its median per predictor bin;
  - predictive: one table per slider value fitted on the other scenes
    (leave-one-scene-out), applied to the LR neutral as y * 2^(g L); user
    criterion on the displayed brightness (mean sRGB-encoded Y) of the 10 %
    brightest pixels of the neutral, prediction / LR - 1, within +-3 %.
    Gain g = 1, or fitted per scene on all its pixels by least squares
    (never on the criterion itself).

Read-only: images are not modified; one immutable JSON is written.
Usage: python scripts/analyse_local_tone.py [--output-dir docs/validation/raw]
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import platform
import sys

import numpy as np
from scipy import ndimage

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis import inputs  # noqa: E402
from analysis.native import inspect, linearize, sha256, write_immutable  # noqa: E402

BASE = ROOT / "tests/image/base"
LUMA = np.array([.2126, .7152, .0722], dtype=np.float32)
FACTOR = 8                       # block mean, native pixels per analysis pixel
FLOOR = 2.0 ** -14
EDGES = np.arange(-14, 0.001, .25)   # log2 brightness bins
SECOND = {"saturation": np.linspace(0, 1, 11), "hue": np.linspace(0, 390, 14),  # last bin: low saturation
          "detail": np.arange(-2, 2.001, .25), "base": EDGES, "rank": np.linspace(0, 1, 11)}
MIN_COUNT = 50
TOLERANCE = .03


def block_mean(path, luminance_only):
    """Block-mean linear sRGB (or its luminance), read by row bands of the memmap."""
    pixels, metadata, _ = inspect(path)
    height, width = (pixels.shape[0] // FACTOR) * FACTOR, (pixels.shape[1] // FACTOR) * FACTOR
    rows = []
    for top in range(0, height, FACTOR * 64):
        band = linearize(np.asarray(pixels[top:min(top + FACTOR * 64, height), :width, :3], dtype=np.float32) / 65535)
        if luminance_only:
            band = (band @ LUMA)[..., None]
        rows.append(band.reshape(-1, FACTOR, width // FACTOR, FACTOR, band.shape[-1]).mean(axis=(1, 3)))
    del pixels
    result = np.concatenate(rows).astype(np.float32)
    return (result[..., 0] if luminance_only else result), metadata


def guided(x, radius, eps):
    """He et al. guided filter of x by itself (edge-preserving smoothing)."""
    box = lambda a: ndimage.uniform_filter(a, size=2 * radius + 1, mode="reflect")
    mean, var = box(x), box(x * x) - box(x) ** 2
    a = var / (var + eps)
    return box(a) * x + box(mean - a * mean)


def log2(v):
    return np.log2(np.maximum(v, FLOOR)).astype(np.float32)


def hue_saturation(rgb):
    high, low = rgb.max(axis=-1), rgb.min(axis=-1)
    chroma = high - low
    saturation = np.where(high > 0, chroma / np.maximum(high, 1e-9), 0)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    safe = np.maximum(chroma, 1e-9)
    hue = np.where(high == r, ((g - b) / safe) % 6, np.where(high == g, (b - r) / safe + 2, (r - g) / safe + 4)) * 60
    return hue, saturation


def predictors(rgb):
    """Name -> (first log2 brightness, optional second coordinate and its edges)."""
    y = rgb @ LUMA
    x = log2(y)
    hue, saturation = hue_saturation(rgb)
    base = guided(x, 16, .25)
    gauss = ndimage.gaussian_filter(x, 16, mode="reflect")
    # Image-relative coordinates, mapped onto the log2 bins: percentile rank
    # (-14..0) and distance to the image's 99th / 99.9th percentiles and median.
    rank = (np.argsort(np.argsort(x, axis=None)).reshape(x.shape) / (x.size - 1)).astype(np.float32)
    relative = lambda q: np.minimum(x - np.quantile(x, q), 0)
    return {
        "Y": (x, None),
        "rank": (-14 + 14 * rank, None),
        "Y_minus_Q99": (relative(.99), None),
        "Y_minus_Q999": (relative(.999), None),
        "Y_minus_median": (np.clip(x - np.quantile(x, .5) - 6, -14, 0), None),
        "Y_x_rank": (x, (rank, "rank")),
        "max_channel": (log2(rgb.max(axis=-1)), None),
        "channel_mean": (log2(rgb.mean(axis=-1)), None),
        "Y_max_geometric": (.5 * x + .5 * log2(rgb.max(axis=-1)), None),
        "guided16_base": (.25 * x + .75 * base, None),
        "Y_x_saturation": (x, (saturation, "saturation")),
        "Y_x_hue": (x, (np.where(saturation > .15, hue, 375.0), "hue")),
        "max_x_saturation": (log2(rgb.max(axis=-1)), (saturation, "saturation")),
        "Y_x_guided16_detail": (x, (x - base, "detail")),
        "Y_x_gauss16_detail": (x, (x - gauss, "detail")),
        "Y_x_guided16_base": (x, (base, "base")),
    }


def bin_index(first, second):
    index = np.clip(np.digitize(first, EDGES) - 1, 0, len(EDGES) - 2).astype(np.int32)
    if second is None:
        return index, len(EDGES) - 1
    values, kind = second
    edges = SECOND[kind]
    j = np.clip(np.digitize(values, edges) - 1, 0, len(edges) - 2)
    return index * (len(edges) - 1) + j, (len(EDGES) - 1) * (len(edges) - 1)


def medians(index, v, count):
    result = np.full(count, np.nan)
    order = np.argsort(index, kind="stable")
    sorted_index, sorted_v = index[order], v[order]
    bounds = np.searchsorted(sorted_index, np.arange(count + 1))
    for i in range(count):
        chunk = sorted_v[bounds[i]:bounds[i + 1]]
        if chunk.size >= MIN_COUNT:
            result[i] = np.median(chunk)
    return result


def fill(table, fallback):
    """Empty cells take the fallback (the 1D curve of the same brightness bin)."""
    return np.where(np.isfinite(table), table, fallback)


def encode(y):
    y = np.clip(y, 0, 1)
    return np.where(y <= .0031308, 12.92 * y, 1.055 * y ** (1 / 2.4) - .055)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == "highlights" and r.get("reference")]
    scenes = sorted({r["scene"] for r in records})
    values = sorted({r["value"] for r in records})
    print(len(scenes), "scenes,", len(records), "LR pairs, values", values, flush=True)

    data, provenance = {}, {}
    for scene in scenes:
        neutral = next(r["reference"] for r in records if r["scene"] == scene)
        rgb, meta = block_mean(neutral, False)
        y0 = rgb @ LUMA
        provenance[scene] = {"neutral": str(Path(neutral).relative_to(BASE)), "neutral_sha256": meta["sha256"],
                             "variants": {}}
        variants = {}
        for r in (r for r in records if r["scene"] == scene):
            y1, meta1 = block_mean(r["file"], True)
            if y1.shape != y0.shape:
                raise ValueError("Geometry differs: " + r["file"])
            variants[r["value"]] = y1
            provenance[scene]["variants"][str(r["value"])] = {"file": str(Path(r["file"]).relative_to(BASE)),
                                                             "sha256": meta1["sha256"]}
        valid = y0 > FLOOR
        top = y0 >= np.quantile(y0, .9)
        index = {}
        for name, (first, second) in predictors(rgb).items():
            cells, count = bin_index(first, second)
            index[name] = cells.astype(np.int16), count  # at most 56 x 16 cells
        del rgb
        coarse = bin_index(log2(y0), None)[0]
        data[scene] = {"y0": y0, "valid": valid, "top": top, "index": index, "coarse": coarse,
                       "e": {v: np.log2(np.maximum(y1, FLOOR / 4) / np.maximum(y0, FLOOR)).astype(np.float32)
                             for v, y1 in variants.items()},
                       "reference": {v: float(encode(y1[top]).mean()) for v, y1 in variants.items()}}
        print("loaded", scene, y0.shape, flush=True)

    names = list(next(iter(data.values()))["index"])
    explanatory = {str(v): {} for v in values}
    predictive = {str(v): {} for v in values}
    for value in values:
        per_scene = [s for s, d in data.items() if value in d["e"]]
        curves1d = {}
        for s in per_scene:
            d = data[s]
            curves1d[s] = medians(d["coarse"][d["valid"]], d["e"][value][d["valid"]], len(EDGES) - 1)
        for name in names:
            rms, tables, sizes = {}, {}, {}
            for s in per_scene:
                d = data[s]
                index, count = d["index"][name]
                i, v = index[d["valid"]], d["e"][value][d["valid"]]
                table = medians(i, v, count)
                ratio = count // (len(EDGES) - 1)
                fallback = np.repeat(np.nan_to_num(curves1d[s]), ratio)
                tables[s], sizes[s] = table, count
                rms[s] = float(np.sqrt(np.mean((v - fill(table, fallback)[i]) ** 2)))
            explanatory[str(value)][name] = {"rms_ev": rms, "median_rms_ev": float(np.median(list(rms.values())))}
            outcome = {}
            for s in per_scene:
                d = data[s]
                others = [o for o in per_scene if o != s]
                with np.errstate(all="ignore"):
                    shared = np.nanmean(np.array([tables[o] for o in others]), axis=0)
                    shared1d = np.nanmean(np.array([curves1d[o] for o in others]), axis=0)
                ratio = sizes[s] // (len(EDGES) - 1)
                shared = fill(shared, np.repeat(np.nan_to_num(shared1d), ratio))
                index = d["index"][name][0]
                lift = np.where(d["valid"], shared[index], 0).astype(np.float32)
                e = d["e"][value]
                mask = d["valid"]
                denominator = float(np.sum(lift[mask] ** 2))
                gain = float(np.sum(e[mask] * lift[mask]) / denominator) if denominator > 0 else 1.0
                top, y0, reference = d["top"], d["y0"][d["top"]], d["reference"][value]
                result = lambda g: float(encode(np.minimum(y0 * 2.0 ** (g * lift[top]), 1)).mean() / reference - 1)
                outcome[s] = {"no_gain": result(1.0), "pixel_gain": result(gain), "gain": gain}
            within = lambda key: int(sum(abs(o[key]) <= TOLERANCE for o in outcome.values()))
            mean = lambda key: float(np.mean([abs(o[key]) for o in outcome.values()]))
            predictive[str(value)][name] = {"scenes": outcome, "within_no_gain": within("no_gain"),
                                            "within_pixel_gain": within("pixel_gain"),
                                            "mean_abs_no_gain": mean("no_gain"), "mean_abs_pixel_gain": mean("pixel_gain")}
        # Ceilings: each scene's own table (best curve of that form for the image)
        # and the shared curve with a per-scene gain and zone shift fitted on pixels.
        ceilings = {}
        for name in ("Y", "Y_x_gauss16_detail"):
            outcome = {}
            for s in per_scene:
                d = data[s]
                index, count = d["index"][name]
                own = medians(index[d["valid"]], d["e"][value][d["valid"]], count)
                ratio = count // (len(EDGES) - 1)
                own = fill(own, np.repeat(np.nan_to_num(curves1d[s]), ratio))
                lift = np.where(d["valid"], own[index], 0)
                top, y0, reference = d["top"], d["y0"][d["top"]], d["reference"][value]
                outcome[s] = float(encode(np.minimum(y0 * 2.0 ** lift[top], 1)).mean() / reference - 1)
            ceilings["own_" + name] = {"scenes": outcome, "within": int(sum(abs(v) <= TOLERANCE for v in outcome.values())),
                                       "mean_abs": float(np.mean([abs(v) for v in outcome.values()]))}
        outcome = {}
        for s in per_scene:
            d = data[s]
            others = [o for o in per_scene if o != s]
            with np.errstate(all="ignore"):
                shared1d = np.nan_to_num(np.nanmean(np.array([curves1d[o] for o in others]), axis=0))
            e, mask, coarse = d["e"][value], d["valid"], d["coarse"]
            best = None
            for shift in range(-12, 13):  # quarter EV bins: -3 to +3 EV
                lift = np.where(mask, shared1d[np.clip(coarse - shift, 0, len(shared1d) - 1)], 0)
                denominator = float(np.sum(lift[mask] ** 2))
                gain = float(np.sum(e[mask] * lift[mask]) / denominator) if denominator > 0 else 0.0
                error = float(np.mean((e[mask] - gain * lift[mask]) ** 2))
                if best is None or error < best[0]:
                    best = (error, shift * .25, gain, lift)
            _, shift, gain, lift = best
            top, y0, reference = d["top"], d["y0"][d["top"]], d["reference"][value]
            outcome[s] = {"criterion": float(encode(np.minimum(y0 * 2.0 ** (gain * lift[top]), 1)).mean() / reference - 1),
                          "shift_ev": shift, "gain": gain}
        ceilings["shared_Y_gain_shift"] = {"scenes": outcome,
                                           "within": int(sum(abs(v["criterion"]) <= TOLERANCE for v in outcome.values())),
                                           "mean_abs": float(np.mean([abs(v["criterion"]) for v in outcome.values()]))}
        predictive[str(value)]["ceilings"] = ceilings
        print(f"HL {value:+.0f} | ceilings: " + ", ".join(f"{k} {v['within']}/{len(per_scene)} ({v['mean_abs']:.1%})"
                                                         for k, v in ceilings.items()), flush=True)
        for name in names:
            e, p = explanatory[str(value)][name], predictive[str(value)][name]
            if name not in ("Y", "rank", "Y_minus_Q99", "Y_minus_Q999", "Y_minus_median", "Y_x_rank",
                            "Y_x_gauss16_detail"):
                continue
            print(f"  {name:22s} RMS {e['median_rms_ev']:.3f} EV | within +-3 %: no gain {p['within_no_gain']:2d}/"
                  f"{len(per_scene)} ({p['mean_abs_no_gain']:.1%}), pixel gain {p['within_pixel_gain']:2d} "
                  f"({p['mean_abs_pixel_gain']:.1%})", flush=True)

    now = datetime.now(timezone.utc)
    result = {"campaign_id": f"analyse-hl-local-v005-{now.strftime('%Y%m%dT%H%M%S%fZ')}", "schema_version": 1,
              "measured_at_utc": now.isoformat(), "group": "lumiere", "feature": "highlights",
              "supersedes_campaign_id": "analyse-hl-local-v004-20261020T100214007790Z",
              "supersedes_reason": "49 scenes instead of 19 (30 new: 21 EOS 60D, 5 R7, 4 R6 Mark III), all LR "
                                   "neutral and Highlights references re-exported on 20 October 2026 with the "
                                   "self-contained presets (IPR Lumieres complet)",
              "question": "What explains Lightroom Highlights beyond the pixel luminance?",
              "method": {"input": "Lightroom pairs neutral -> Highlights variant, pixel-aligned",
                         "downsampling": f"block mean {FACTOR}x{FACTOR} of linear sRGB",
                         "effect": "e = log2(Y1 / Y0), Rec.709 luminance of linear sRGB", "floor": FLOOR,
                         "bins": {"log2 brightness": [float(EDGES[0]), float(EDGES[-1]), .25],
                                  **{k: [float(v[0]), float(v[-1]), len(v) - 1] for k, v in SECOND.items()}},
                         "min_count_per_bin": MIN_COUNT,
                         "explanatory": "per scene RMS of e around its median per predictor bin (empty 2D cells: 1D median)",
                         "predictive": "leave-one-scene-out mean of per-scene tables; y * 2^(g L), clipped at 1",
                         "gain": "g = 1, or least squares of e on L over all valid pixels of the scene",
                         "criterion": "mean sRGB-encoded Y of the 10 % brightest neutral pixels, prediction / LR - 1",
                         "tolerance": TOLERANCE,
                         "ceilings": "own_<predictor>: the scene's own median table applied to itself (best curve of "
                                     "that form for the image); shared_Y_gain_shift: leave-one-out shared curve, "
                                     "gain and zone shift (-3..+3 EV) fitted on the scene's pixels by least squares"},
              "limits": ["Simulated on the LR neutral: isolates the operator, not IPR's development",
                         "Luminance effect only; colour handling (darkenKeepingColour) not modelled",
                         "The pixel gain is fitted on the scene itself: an upper bound for a gain predictor",
                         "Two-dimensional tables have more cells: compare predictive results, not only RMS",
                         f"Analysis at 1/{FACTOR} resolution: detail finer than {FACTOR} px is not assessed"],
              "software": {"python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                           "script_sha256": sha256(Path(__file__))},
              "scenes": scenes, "values": values, "provenance": provenance,
              "explanatory": explanatory, "predictive": predictive}
    output = args.output_dir / (result["campaign_id"] + ".json")
    write_immutable(output, result)
    print(output)


if __name__ == "__main__":
    main()
