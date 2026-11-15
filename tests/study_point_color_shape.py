"""Shape of Lightroom's point colour neighbourhood: how the hue, chroma and lightness distances combine.

On the saturation -100 exports of couleur-point, per source and range, the weight
w = (1 - C/C0) / A (study_point_color) of every pixel is fitted from its distances
to the source in Oklab (hue difference, log chroma ratio, lightness difference),
each scaled by its own half-width on each side (six widths) and an overall scale,
with the smoothstep falloff f, under three combinations:

- product:   w = k f(t_h) f(t_c) f(t_l)
- euclidean: w = k f(sqrt(t_h^2 + t_c^2 + t_l^2))
- maximum:   w = k f(max(t_h, t_c, t_l))

t = |distance| / (2 width), so that each width is where the weight falls to 0.5 along
its axis alone. Least squares on a fixed random sample of pixels; RMS residual
per model, and its leave-one-scene-out value for the chosen widths. The sources'
Oklab colours come from the latest point colour study. Writes one immutable,
versioned JSON.

Usage: python tests/study_point_color_shape.py --data-version N [--study JSON]
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
from scipy.optimize import least_squares

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from study_grading_response import load, oklab  # noqa: E402
from study_point_color import CLIP, amplitude, axes, desaturation_weight, embedded_xmp, point_state  # noqa: E402
from study_sources import git  # noqa: E402

STRIDE = 12
SAMPLE = 120000
MODELS = ("product", "euclidean", "maximum")


def falloff(t):
    t = np.minimum(t, 1.0)
    return 1.0 - t * t * (3.0 - 2.0 * t)


def scaled(distances, widths):
    """|distance| / (2 width) per axis, the width of the distance's side."""
    out = []
    for i, axis in enumerate(("hue", "chroma", "lightness")):
        d = distances[axis]
        width = np.where(d < 0, widths[2 * i], widths[2 * i + 1])
        out.append(np.abs(d) / (2.0 * np.maximum(width, 1e-6)))
    return out


def predict(model, distances, parameters):
    k, widths = parameters[0], parameters[1:]
    th, tc, tl = scaled(distances, widths)
    if model == "product":
        return k * falloff(th) * falloff(tc) * falloff(tl)
    if model == "euclidean":
        return k * falloff(np.sqrt(th * th + tc * tc + tl * tl))
    return k * falloff(np.maximum(np.maximum(th, tc), tl))


def fit(model, distances, w, start):
    lower = [0.5, 2.0, 2.0, 0.02, 0.02, 0.01, 0.01]
    upper = [1.5, 180.0, 180.0, 5.0, 5.0, 1.0, 1.0]
    result = least_squares(lambda p: predict(model, distances, p) - w, np.clip(start, lower, upper),
                           bounds=(lower, upper), x_scale="jac")
    return result.x


def rms(model, distances, w, parameters):
    return float(np.sqrt(np.mean((predict(model, distances, parameters) - w) ** 2)))


def subset(distances, index):
    return {axis: values[index] for axis, values in distances.items()}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--study", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-couleur-point-forme-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    study_path = args.study or Path(sorted(glob.glob(str(args.output_dir / "etude-couleur-point-v*.json")))[-1])
    study = json.loads(study_path.read_text(encoding="utf-8"))["sources"]
    by_source = {tuple(s["lightroom_source"]): s for s in study.values()}
    start = time.monotonic()
    neutrals = {}
    for p in sorted((args.base / "grading" / "neutre").glob("*.tif")):
        flat = load(p).reshape(-1, 3)
        idx = np.flatnonzero(flat.max(1) < CLIP)[::STRIDE]
        neutrals[p.stem.split("-")[0]] = (idx, oklab(flat[idx]))
    files = collections.defaultdict(dict)   # (source, range) -> scene -> path
    for p in sorted((args.base / "couleur-point").glob("*.tif")):
        state = point_state(embedded_xmp(p))
        scene = p.stem.split("-")[0]
        if (state and scene in neutrals and state["saturation"] == -100 and not state["hue"]
                and not state["luminance"] and not state["variance"] and tuple(state["source"]) in by_source):
            files[(state["source"], state["range"])].setdefault(scene, p)

    rng = np.random.default_rng(20261027)
    results = collections.defaultdict(dict)
    for (source, range_), scenes in sorted(files.items()):
        record = by_source[tuple(source)]
        peak = record["saturation_minus_100_amplitude"]
        lab0, w, scene_of = [], [], []
        for scene, path in sorted(scenes.items()):
            idx, lab = neutrals[scene]
            flat = load(path).reshape(-1, 3)[idx]
            lab1 = oklab(flat)
            lab1[flat.max(1) >= CLIP] = np.nan
            loss = desaturation_weight(lab, lab1)
            keep = np.isfinite(loss)
            lab0.append(lab[keep])
            w.append(np.clip(loss[keep] / peak, 0.0, 1.0))
            scene_of += [scene] * int(keep.sum())
        lab0, w, scene_of = np.concatenate(lab0), np.concatenate(w), np.array(scene_of)
        pick = rng.choice(len(w), size=min(SAMPLE, len(w)), replace=False)
        lab0, w, scene_of = lab0[pick], w[pick], scene_of[pick]
        distances = axes(lab0, record["oklab_source"])
        marginal = record["ranges"].get(str(range_), {})
        widths = []
        for axis, default in (("hue", 38.0), ("chroma", 0.9), ("lightness", 0.26)):
            sides = marginal.get(axis, {}).get("half_width_0_5", [None, None])
            widths += [s if s else default for s in sides]
        entry = {"scenes": len(scenes), "pixels": int(len(w)), "models": {}}
        for model in MODELS:
            parameters = fit(model, distances, w, [1.0] + widths)
            # Leave one scene out: fit on the others, score the held-out scene.
            held = []
            for scene in sorted(set(scene_of))[::4]:
                train, test = scene_of != scene, scene_of == scene
                if test.sum() < 500:
                    continue
                p = fit(model, subset(distances, train), w[train], parameters)
                held.append(rms(model, subset(distances, test), w[test], p))
            entry["models"][model] = {
                "scale": round(float(parameters[0]), 4),
                "hue_widths": [round(float(v), 3) for v in parameters[1:3]],
                "chroma_widths": [round(float(v), 4) for v in parameters[3:5]],
                "lightness_widths": [round(float(v), 4) for v in parameters[5:7]],
                "rms": round(rms(model, distances, w, parameters), 5),
                "held_out_rms_median": round(float(np.median(held)), 5) if held else None}
        results[f"{source[0]:.4f}"][str(range_)] = {"oklab_source_lch": record["oklab_source_lch"], **entry}
        print(f"{source[0]:.4f} G{range_}: " + ", ".join(
            f"{m} {entry['models'][m]['rms']:.4f} (held {entry['models'][m]['held_out_rms_median']})" for m in MODELS),
            flush=True)

    record = {"campaign_id": campaign, "runner": "tests/study_point_color_shape.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "study": study_path.name, "models": list(MODELS), "falloff": "smoothstep 1 - 3t^2 + 2t^3, t = d / (2 width)",
              "sample_stride": STRIDE, "sample_pixels": SAMPLE, "seed": 20261027,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "sources": results}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
