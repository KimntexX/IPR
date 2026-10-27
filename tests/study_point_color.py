"""Lightroom's point colour on its exports: neighbourhood of a picked colour and response of each slider.

Exports of couleur-point (one state per preset, read from the settings Lightroom
embeds: crs:PointColors and crs:ColorVariance) against the neutral of the same
geometry (grading/neutre), in Oklab, reduced by 2 as the grading study:

- Neighbourhood: saturation -100 removes the chroma in proportion to a pixel's
  weight, so w = 1 - C/C0 (neutral chroma C0 above 0.02). The source is the median
  Oklab colour of the pixels at w >= 0.9 at range 50. Per range, profiles of w
  along the hue difference, the log chroma ratio and the lightness difference to
  the source, each taken near the source on the other two axes, and the distances
  where the profile falls to 0.5 and to 0.05 on each side.
- Responses at range 50 (the weight of saturation -100 at range 50): the change at
  w = 1, least squares through the origin over w: hue rotation (degrees), log chroma
  gain and log lightness gain; variance as the gain of the hue deviation and of the
  log chroma deviation from the source.
- Per scene, the hue half-width at 0.5 and range 50, to see whether the
  neighbourhood is absolute in colour or follows the image.

Pixels with a channel above 0.98 in the neutral or the state are left out; a
repeated export counts once. Writes one immutable, versioned JSON.

Usage: python tests/study_point_color.py --data-version N
"""
import argparse
import collections
import json
import platform
import sys
import time
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from study_grading_response import load, oklab  # noqa: E402
from study_sources import git  # noqa: E402

CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"
RDF = "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
STRIDE = 8
GREY = 0.02
CLIP = 0.98
# Tolerances near the source on the axes a profile does not follow.
NEAR = {"hue": 12.0, "chroma": 0.25, "lightness": 0.08}
EDGES = {"hue": np.arange(-90, 91, 5.0), "chroma": np.arange(-1.5, 1.51, 0.1),
         "lightness": np.arange(-0.4, 0.41, 0.025)}
MIN_PIXELS = 30


def point_state(xmp):
    """Source (as Lightroom writes it) and sliders of the one point of an export; None without one."""
    root = ET.fromstring(xmp)
    items = {tag: [li.text.strip() for e in root.iter(f"{{{CRS}}}{tag}") for li in e.iter(f"{{{RDF}}}li")
                   if li.text and li.text.strip()] for tag in ("PointColors", "ColorVariance")}
    if len(items["PointColors"]) != 1:
        return None
    values = [float(v) for v in items["PointColors"][0].split(",")]
    if len(values) != 19 or values[0] < 0:   # an empty point is written as -1
        return None
    variance = float(items["ColorVariance"][0]) if items["ColorVariance"] else 0.0
    return {"source": tuple(round(v, 6) for v in values[:3]), "hue": round(values[3] * 100),
            "saturation": round(values[4] * 100), "luminance": round(values[5] * 100),
            "range": round(values[6] * 100), "variance": round(variance * 100)}


def embedded_xmp(path):
    with Image.open(path) as image:
        xmp = image.tag_v2.get(700, b"")
    return bytes(xmp) if isinstance(xmp, tuple) else xmp


def lch(lab):
    """Lightness, chroma and hue (degrees) of Oklab colours (..., 3)."""
    return lab[..., 0], np.hypot(lab[..., 1], lab[..., 2]), np.degrees(np.arctan2(lab[..., 2], lab[..., 1]))


def hue_difference(h, reference):
    return (h - reference + 180.0) % 360.0 - 180.0


def axes(lab0, source):
    """Hue difference (degrees), log chroma ratio and lightness difference of each colour to the source."""
    L, C, h = lch(lab0)
    Ls, Cs, hs = lch(np.asarray(source, dtype=np.float64))
    return {"hue": hue_difference(h, hs), "chroma": np.log(np.maximum(C, 1e-6) / max(Cs, 1e-6)),
            "lightness": L - Ls}


def desaturation_weight(lab0, lab1):
    """Weight of each pixel under saturation -100: its share of chroma removed; NaN on greys."""
    C0, C1 = np.hypot(lab0[:, 1], lab0[:, 2]), np.hypot(lab1[:, 1], lab1[:, 2])
    w = np.clip(1.0 - C1 / np.maximum(C0, 1e-6), 0.0, 1.0)
    return np.where(C0 > GREY, w, np.nan)


def estimate_source(lab0, w, level=0.9):
    sel = np.isfinite(w) & (w >= level)
    return np.median(lab0[sel], axis=0) if sel.sum() >= MIN_PIXELS else None


def profile(distance, w, edges):
    """Mean weight and count per bin of a distance."""
    ok = np.isfinite(w) & np.isfinite(distance)
    index = np.digitize(distance[ok], edges) - 1
    inside = (index >= 0) & (index < len(edges) - 1)
    count = np.bincount(index[inside], minlength=len(edges) - 1)
    total = np.bincount(index[inside], w[ok][inside], minlength=len(edges) - 1)
    mean = np.where(count >= MIN_PIXELS, total / np.maximum(count, 1), np.nan)
    return (edges[:-1] + edges[1:]) / 2, mean, count


def crossing(centres, mean, level):
    """Distance on each side (negative, positive) where the profile first falls to the level; None when it does not."""
    result = []
    for side in (-1, 1):
        order = np.argsort(side * centres)
        xs, ys = side * centres[order], mean[order]
        keep = (xs >= 0) & np.isfinite(ys)
        xs, ys = xs[keep], ys[keep]
        found = None
        for i in range(1, len(xs)):
            if ys[i - 1] > level >= ys[i]:
                found = float(xs[i - 1] + (ys[i - 1] - level) / (ys[i - 1] - ys[i]) * (xs[i] - xs[i - 1]))
                break
        result.append(found)
    return result


def neighbourhood(lab0, w, source):
    distances = axes(lab0, source)
    report = {}
    for axis, edges in EDGES.items():
        near = np.ones(len(w), dtype=bool)
        for other, tolerance in NEAR.items():
            if other != axis:
                near &= np.abs(distances[other]) <= tolerance
        centres, mean, count = profile(distances[axis][near], w[near], edges)
        report[axis] = {"centres": centres.round(4).tolist(),
                        "mean_weight": [None if not np.isfinite(m) else round(float(m), 4) for m in mean],
                        "pixels": count.tolist(), "half_width_0_5": crossing(centres, mean, 0.5),
                        "reach_0_05": crossing(centres, mean, 0.05)}
    return report


def slope(w, change):
    """Change at w = 1: least squares through the origin over the pixels of positive weight."""
    ok = np.isfinite(w) & np.isfinite(change) & (w > 0.05)
    if ok.sum() < MIN_PIXELS:
        return None
    return float(np.sum(w[ok] * change[ok]) / np.sum(w[ok] ** 2))


def change_of(component, lab0, lab1, source):
    """Per-pixel change a slider is read on."""
    L0, C0, h0 = lch(lab0)
    L1, C1, h1 = lch(lab1)
    if component == "hue":
        return np.where(C0 > GREY, hue_difference(h1, h0), np.nan)
    if component == "saturation":
        return np.where(C0 > GREY, np.log(np.maximum(C1, 1e-6) / C0), np.nan)
    if component == "luminance":
        return np.log(np.maximum(L1, 1e-6) / np.maximum(L0, 1e-6))
    # Variance: gain of the deviation from the source, in hue and in log chroma.
    before, after = axes(lab0, source), axes(lab1, source)
    ok = (C0 > GREY) & (np.abs(before["hue"]) > 2.0)
    return np.stack([np.where(ok, after["hue"] / np.where(ok, before["hue"], 1.0) - 1.0, np.nan),
                     np.where(ok & (np.abs(before["chroma"]) > 0.05),
                              after["chroma"] / np.where(np.abs(before["chroma"]) > 0.05, before["chroma"], 1.0) - 1.0,
                              np.nan)], axis=1)


def single_slider(state):
    """(component, value) of a state with one slider moved at range 50; None otherwise."""
    moved = [(c, state[c]) for c in ("hue", "saturation", "luminance", "variance") if state[c]]
    return moved[0] if len(moved) == 1 and state["range"] == 50 else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-couleur-point-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    neutrals = {}
    for p in sorted((args.base / "grading" / "neutre").glob("*.tif")):
        rgb = load(p)
        flat = rgb.reshape(-1, 3)
        idx = np.flatnonzero(flat.max(1) < CLIP)[::STRIDE]
        neutrals[p.stem.split("-")[0]] = (rgb.shape, idx, oklab(flat[idx]))
    # source -> state key -> scene -> (lab1 on the neutral's samples, NaN where clipped)
    exports = collections.defaultdict(lambda: collections.defaultdict(dict))
    skipped = []
    for p in sorted((args.base / "couleur-point").rglob("*.tif")):
        state = point_state(embedded_xmp(p))
        scene = p.stem.split("-")[0]
        if state is None or scene not in neutrals:
            skipped.append(p.name)
            continue
        key = (state["hue"], state["saturation"], state["luminance"], state["variance"], state["range"])
        if scene in exports[state["source"]][key]:
            continue
        shape, idx, _ = neutrals[scene]
        rgb = load(p)
        if rgb.shape != shape:
            raise ValueError("Geometry differs from the neutral: " + p.name)
        flat = rgb.reshape(-1, 3)[idx]
        lab1 = oklab(flat)
        lab1[flat.max(1) >= CLIP] = np.nan
        exports[state["source"]][key][scene] = lab1.astype(np.float32)
    print(f"{sum(len(s) for v in exports.values() for s in v.values())} exports, {len(exports)} sources, "
          f"{len(skipped)} skipped", flush=True)

    sources = {}
    for source, states in sorted(exports.items()):
        label = "/".join(f"{v:.4f}" for v in source)
        reference = states.get((0, -100, 0, 0, 50), {})
        weights = {scene: desaturation_weight(neutrals[scene][2], lab1) for scene, lab1 in reference.items()}
        if not weights:
            print(f"{label}: no saturation -100 at range 50, skipped", flush=True)
            continue
        all_lab0 = np.concatenate([neutrals[s][2] for s in weights])
        all_w = np.concatenate(list(weights.values()))
        oklab_source = estimate_source(all_lab0, all_w)
        if oklab_source is None:
            print(f"{label}: weight never reaches 0.9, skipped", flush=True)
            continue
        record = {"lightroom_source": list(source), "oklab_source": oklab_source.round(5).tolist(),
                  "oklab_source_lch": [round(float(v), 4) for v in lch(oklab_source)],
                  "scenes": sorted(weights), "ranges": {}, "responses": {}, "per_scene_hue_half_width": {}}
        for (hue, sat, lum, var, rng), scenes in sorted(states.items()):
            if (hue, sat, lum, var) != (0, -100, 0, 0):
                continue
            lab0 = np.concatenate([neutrals[s][2] for s in scenes])
            w = np.concatenate([desaturation_weight(neutrals[s][2], scenes[s]) for s in scenes])
            record["ranges"][str(rng)] = {"scenes": len(scenes), **neighbourhood(lab0, w, oklab_source)}
            print(f"{label} range {rng}: hue half-width {record['ranges'][str(rng)]['hue']['half_width_0_5']}",
                  flush=True)
        for scene, w in weights.items():
            hue = neighbourhood(neutrals[scene][2], w, oklab_source)["hue"]["half_width_0_5"]
            record["per_scene_hue_half_width"][scene] = hue
        for key, scenes in sorted(states.items()):
            moved = single_slider(dict(zip(("hue", "saturation", "luminance", "variance", "range"), key)))
            if moved is None or moved == ("saturation", -100):
                continue
            component, value = moved
            common = [s for s in scenes if s in weights]
            if not common:
                continue
            w = np.concatenate([weights[s] for s in common])
            change = np.concatenate([change_of(component, neutrals[s][2], scenes[s], oklab_source) for s in common])
            entry = ({"hue_deviation_gain": slope(w, change[:, 0]), "chroma_deviation_gain": slope(w, change[:, 1])}
                     if component == "variance" else {"at_weight_1": slope(w, change)})
            record["responses"].setdefault(component, {})[str(value)] = {"scenes": len(common), **entry}
        sources[label] = record

    result = {"campaign_id": campaign, "runner": "tests/study_point_color.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "w = 1 - C/C0 under saturation -100; responses as change at w = 1 (least squares through 0)",
              "units": {"hue": "degrees", "chroma": "natural log of the chroma ratio", "lightness": "Oklab L",
                        "hue_response": "degrees", "saturation_response": "log chroma gain",
                        "luminance_response": "log lightness gain"},
              "sample_stride": STRIDE, "near_tolerances": NEAR, "grey_chroma": GREY, "clip": CLIP,
              "skipped": skipped, "duration_seconds": round(time.monotonic() - start, 1),
              "python": platform.python_version(), "sources": sources}
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{result['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
