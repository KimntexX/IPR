#!/usr/bin/env python3
"""Audit noise TIFF campaigns and measure contiguous native-pixel regions.

No RAW, TIFF, sidecar or preferences are modified. Statistics describe output
noise/detail proxies, not a clean-ground-truth reconstruction score.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from analyse_color_mixer import linearize, luminance, metadata, native_pixels

VALUES = (25, 50, 75, 100)
IDS = ("6V1A0043", "6V1A0112", "6V1A0327", "6V1A0624", "6V1A9890")
CONTROL = {"luminance": "LuminanceSmoothing", "couleur": "ColorNoiseReduction"}
ALLOWED = {"LuminanceSmoothing", "ColorNoiseReduction", "LuminanceNoiseReductionDetail",
           "LuminanceNoiseReductionContrast", "ColorNoiseReductionDetail", "ColorNoiseReductionSmoothness"}
PROVENANCE = {"RawFileName", "HasSettings", "AlreadyApplied", "Version"}


def variant_path(root, image_id, component, engine, number):
    suffix = "" if number == 1 else f"-{number}"
    filename = f"{image_id}{suffix}.tif" if engine == "LR" else f"{image_id}-IPR{suffix}.tiff"
    return root / "bruit" / component / filename


def neutral_path(root, image_id, engine):
    filename = f"{image_id}.tif" if engine == "LR" else f"{image_id}-IPR.tiff"
    return root / "neutre" / filename


def number(settings, key):
    return float(settings.get(key, 0))


def ai_active(settings):
    # Absence is not proof that no upstream enhancement was used.
    return {k: v for k, v in settings.items() if "denois" in k.lower() or "enhance" in k.lower()
            if v.lower() not in ("0", "0.0", "false", "", "none")}


def audit_lr(entry, component=None, value=0):
    settings = entry["settings"]
    issues = []
    if not all(key in settings for key in (*CONTROL.values(), "Sharpness")) or "_parse_error" in settings:
        issues.append({"type": "missing_or_invalid_noise_metadata"})
    expected = {key: value if component and key == CONTROL[component] else 0 for key in CONTROL.values()}
    observed = {key: number(settings, key) for key in CONTROL.values()}
    if expected != observed:
        issues.append({"type": "wrong_noise_state", "expected": expected, "observed": observed})
    if number(settings, "Sharpness") != 0:
        issues.append({"type": "nonzero_sharpening", "amount": number(settings, "Sharpness")})
    if number(settings, "HDREditMode") != 0:
        issues.append({"type": "hdr_not_sdr"})
    if ai_active(settings):
        issues.append({"type": "ai_settings_present", "settings": ai_active(settings)})
    if entry.get("active_mixer"):
        issues.append({"type": "nonzero_color_mixer", "settings": entry["active_mixer"]})
    return issues


def photographic_metadata(path):
    with Image.open(path) as image:
        exif = image.getexif()
        details = exif.get_ifd(34665) if 34665 in exif else {}
        return {"make": str(exif.get(271, "unknown")), "model": str(exif.get(272, "unknown")),
                "ISO": details.get(34855),
                "exposure_seconds": float(details[33434]) if 33434 in details else None,
                "aperture": float(details[33437]) if 33437 in details else None}


def settings_differences(reference, variant):
    ignored = ALLOWED | PROVENANCE
    first, second = reference["settings"], variant["settings"]
    return {key: {"reference": first.get(key), "variant": second.get(key)}
            for key in first.keys() | second.keys() if key not in ignored and first.get(key) != second.get(key)}


def variant_entry(path, engine, component, value, reference, index=None, extra=False):
    entry = {"engine": engine, "component": component, "number": index, "value": value,
             "extra_state": extra}
    if not path.exists():
        entry.update(path=str(path), missing=True, protocol_valid=False)
        return entry, [{"type": "missing_variant", **entry}]
    entry.update(metadata(path))
    entry["file_mtime_ns"] = path.stat().st_mtime_ns
    entry["photographic"] = photographic_metadata(path)
    entry["observed_value"] = number(entry["settings"], CONTROL[component]) if engine == "LR" else value
    problems = audit_lr(entry, component, value) if engine == "LR" else []
    if engine == "LR" and reference:
        differences = settings_differences(reference, entry)
        if differences:
            problems.append({"type": "non_noise_settings_differ", "differences": differences})
    entry["state_valid"] = not any(p["type"] in ("wrong_noise_state", "ai_settings_present", "missing_or_invalid_noise_metadata") for p in problems)
    entry["protocol_valid"] = not problems and bool(reference and reference["protocol_valid"])
    return entry, [{"path": str(path), **p} for p in problems]


def inventory(root, image_ids, mappings=None, neutral_mappings=None, extra_states=None):
    mappings = mappings or {}
    neutral_mappings = neutral_mappings or {}
    extra_states = extra_states or []
    result = {"schema": 2, "utc": datetime.now(timezone.utc).isoformat(), "root": str(root),
              "values": list(VALUES), "scenes": {}, "issues": []}
    result["mapping_overrides"] = [{"image_id": key[0], "component": key[1], "engine": key[2],
                                    "number": key[3], "path": str(path)} for key, path in mappings.items()]
    result["neutral_mapping_overrides"] = [{"image_id": key[0], "engine": key[1], "path": str(path)}
                                          for key, path in neutral_mappings.items()]
    result["extra_state_mappings"] = [dict(entry, path=str(entry["path"])) for entry in extra_states]
    for image_id in image_ids:
        group = {"neutrals": {}, "variants": []}
        for engine in ("IPR", "LR"):
            path = neutral_mappings.get((image_id, engine), neutral_path(root, image_id, engine))
            if path.exists():
                entry = metadata(path)
                entry["file_mtime_ns"] = path.stat().st_mtime_ns
                entry["engine"] = engine
                entry["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
                problems = audit_lr(entry) if engine == "LR" else []
                entry["protocol_valid"] = not problems
                group["neutrals"][engine] = entry
                result["issues"].extend({"image_id": image_id, "path": str(path), **p} for p in problems)
            else:
                result["issues"].append({"type": "missing_neutral", "image_id": image_id,
                                         "engine": engine, "path": str(path)})
        for component in CONTROL:
            for engine in ("IPR", "LR"):
                reference = group["neutrals"].get(engine)
                for index, value in enumerate(VALUES, 1):
                    path = mappings.get((image_id, component, engine, index), variant_path(root, image_id, component, engine, index))
                    entry, problems = variant_entry(path, engine, component, value, reference, index)
                    result["issues"].extend({"image_id": image_id, **p} for p in problems)
                    group["variants"].append(entry)
        for extra in extra_states:
            if extra["image_id"] == image_id:
                engine = extra["engine"]
                entry, problems = variant_entry(Path(extra["path"]), engine, extra["component"],
                                                extra["value"], group["neutrals"].get(engine), extra=True)
                result["issues"].extend({"image_id": image_id, **p} for p in problems)
                group["variants"].append(entry)
        for entry in list(group["neutrals"].values()) + group["variants"]:
            if not entry.get("missing") and ("srgb" not in entry["icc"].lower() or
                    entry["bits"] != [16] * entry["samples"] or entry["orientation"] != 1):
                    raise ValueError(f"Expected native sRGB 16-bit orientation-1 TIFF: {entry['path']}")
        used = {entry["path"] for entry in group["variants"]}
        group["unmapped_files"] = [metadata(path) for component in CONTROL
            for path in sorted((root / "bruit" / component).glob(f"{image_id}*.tif*")) if str(path) not in used]
        result["scenes"][image_id] = group
    return result


def noise_sigma(plane):
    """Unit-norm Haar diagonal HH MAD; correlated/processed noise is only a proxy."""
    height, width = (plane.shape[0] // 2) * 2, (plane.shape[1] // 2) * 2
    if min(height, width) < 2:
        raise ValueError("Noise regions need at least 2x2 adjacent pixels")
    p = plane[:height, :width]
    hh = (p[::2, ::2] - p[::2, 1::2] - p[1::2, ::2] + p[1::2, 1::2]) * .5
    return float(np.median(np.abs(hh - np.median(hh))) / .6744897501960817)


def blur(plane):
    # Separable binomial low-pass [1,4,6,4,1]/16; no scipy/OpenCV dependency.
    kernel = (1., 4., 6., 4., 1.)
    vertical = np.pad(plane, ((2, 2), (0, 0)), mode="edge")
    smooth = sum(k * vertical[i:i + plane.shape[0]] for i, k in enumerate(kernel)) / 16.
    horizontal = np.pad(smooth, ((0, 0), (2, 2)), mode="edge")
    return sum(k * horizontal[:, i:i + plane.shape[1]] for i, k in enumerate(kernel)) / 16.


def gradient(plane):
    gy, gx = np.gradient(blur(blur(plane)))
    return np.hypot(gx, gy)


def read_patch(pixels, roi):
    top, left, size = roi["top"], roi["left"], roi["size"]
    encoded = np.array(pixels[top:top + size, left:left + size, :3], dtype=np.float32)
    return encoded / np.iinfo(pixels.dtype).max


def region_candidates(pixels, size=256):
    height, width = pixels.shape[:2]
    size = min(size, height, width)
    if size < 16:
        raise ValueError("Regions must be at least 16 pixels across")
    candidates = []
    for fy in np.linspace(.05, .95, 10):
        for fx in np.linspace(.05, .95, 8):
            roi = {"top": int((height - size) * fy), "left": int((width - size) * fx), "size": size}
            encoded = read_patch(pixels, roi)
            y = luminance(linearize(encoded))
            mean = float(y.mean())
            roi.update(mean_Y=mean, structure=float(gradient(y).mean()) / max(mean, .01),
                       sigma_Y=noise_sigma(y), clipping=float((encoded >= 1. - 1./65535.).any(axis=2).mean()))
            candidates.append(roi)
    return candidates


def select_regions(candidates, kind, limit=6):
    eligible = [r for r in candidates if .001 < r["mean_Y"] < .8 and r["clipping"] < .02]
    ranked = sorted(eligible, key=lambda r: r["structure"], reverse=kind == "detail")
    selected = []
    for roi in ranked:
        if all(abs(roi["top"] - other["top"]) >= roi["size"] or
               abs(roi["left"] - other["left"]) >= roi["size"] for other in selected):
            selected.append(dict(roi, kind=kind))
        if len(selected) == limit:
            break
    return selected


def plane_metrics(encoded):
    rgb = linearize(encoded)
    red, green, blue = (rgb[..., i] for i in range(3))
    y = luminance(rgb)
    ycocg = .25 * red + .5 * green + .25 * blue
    co, cg = red - blue, green - .5 * (red + blue)
    co_sigma, cg_sigma = noise_sigma(co), noise_sigma(cg)
    return {"sigma_Y": noise_sigma(y), "sigma_YCoCg": noise_sigma(ycocg),
            "sigma_Co": co_sigma, "sigma_Cg": cg_sigma,
            "sigma_chroma": float(np.hypot(co_sigma, cg_sigma) / np.sqrt(2.)),
            "mean_Y": float(y.mean()), "mean_Co": float(co.mean()), "mean_Cg": float(cg.mean()),
            "gradient_mean": float(gradient(y).mean()),
            "clipped_percent": 100. * float((encoded >= 1. - 1./65535.).any(axis=2).mean()),
            "black_percent": 100. * float((encoded <= 1./65535.).all(axis=2).mean())}


def safe_ratio(first, second):
    return float(first / second) if second > 1e-8 else None


def compare_patch(reference, variant):
    first, second = plane_metrics(reference), plane_metrics(variant)
    y0, y1 = luminance(linearize(reference)), luminance(linearize(variant))
    g0, g1 = gradient(y0), gradient(y1)
    edges = g0 >= max(float(np.quantile(g0, .85)), 1e-6)
    return {"reference": first, "variant": second,
            "ratios": {key: safe_ratio(second[key], first[key])
                       for key in ("sigma_Y", "sigma_YCoCg", "sigma_chroma", "gradient_mean", "mean_Y")},
            "edge_gradient_ratio": safe_ratio(float(g1[edges].mean()), float(g0[edges].mean())) if edges.any() else None,
            "MAE_linear_to_own_reference": float(np.abs(linearize(variant) - linearize(reference)).mean()),
            "mean_Co_drift": second["mean_Co"] - first["mean_Co"],
            "mean_Cg_drift": second["mean_Cg"] - first["mean_Cg"],
            "identical_on_region": bool(np.array_equal(reference, variant))}


def summarize(patches):
    flat = [p for p in patches if p["roi"]["kind"] == "flat"]
    detail = [p for p in patches if p["roi"]["kind"] == "detail"]
    def median(values):
        values = [v for v in values if v is not None]
        return float(np.median(values)) if values else None
    return {"flat_regions": len(flat), "detail_regions": len(detail),
            "flat_sigma_Y_ratio": median([p["ratios"]["sigma_Y"] for p in flat]),
            "flat_sigma_YCoCg_ratio": median([p["ratios"]["sigma_YCoCg"] for p in flat]),
            "flat_sigma_chroma_ratio": median([p["ratios"]["sigma_chroma"] for p in flat]),
            "flat_mean_Y_ratio": median([p["ratios"]["mean_Y"] for p in flat]),
            "detail_edge_gradient_ratio": median([p["edge_gradient_ratio"] for p in detail]),
            "all_regions_identical": all(p["identical_on_region"] for p in patches),
            "max_abs_chroma_mean_drift": max((max(abs(p["mean_Co_drift"]), abs(p["mean_Cg_drift"])) for p in patches), default=None)}


def preview(pixels, rois, path):
    height, width = pixels.shape[:2]
    step = max(1, int(np.ceil(max(height, width) / 1000)))
    values = np.array(pixels[::step, ::step, :3], dtype=np.float32) / np.iinfo(pixels.dtype).max
    image = Image.fromarray(np.round(values * 255.).astype(np.uint8))
    draw = ImageDraw.Draw(image)
    for index, roi in enumerate(rois):
        color = "#00ff88" if roi["kind"] == "flat" else "#ff9933"
        x, y, size = roi["left"] / step, roi["top"] / step, roi["size"] / step
        draw.rectangle((x, y, x + size, y + size), outline=color, width=2)
        draw.text((x, y), f"{roi['kind'][0]}{index}", fill=color)
    image.save(path)


def analyse(result, size, previews=None):
    result["measurement"] = {"method": "native contiguous regions; sRGB->linear; unit-norm Haar HH MAD",
                             "analysis_version": 2, "patch_size": size, "baseline_policy": "own neutral if valid; LR non-neutral/missing -> first observed state (not zero)",
                             "limitations": ["No clean ground truth", "Demosaic and processing correlate noise",
                                             "Automatic regions require visual review", "Noise and real fine texture are not perfectly separable",
                                             "No direct cross-engine pixelwise MAE; different rendering and geometry"]}
    result["results"] = {}
    for image_id, scene in result["scenes"].items():
        groups = {}
        for engine in ("IPR", "LR"):
            for component in CONTROL:
                entries = sorted([e for e in scene["variants"] if e["engine"] == engine and e["component"] == component and not e.get("missing")],
                                 key=lambda e: e["observed_value"])
                if not entries:
                    continue
                neutral = scene["neutrals"].get(engine)
                noise_neutral = neutral and (engine == "IPR" or not any(p["type"] == "wrong_noise_state" for p in audit_lr(neutral)))
                anchor = neutral if noise_neutral else entries[0]
                baseline_value = 0 if noise_neutral else anchor["observed_value"]
                path = Path(anchor["path"])
                before_anchor = path.stat()
                if (before_anchor.st_size, before_anchor.st_mtime_ns) != (anchor["bytes"], anchor["file_mtime_ns"]):
                    raise ValueError(f"Reference changed since metadata audit: {path}")
                pixels = native_pixels(path)
                candidates = region_candidates(pixels, size)
                flats = select_regions(candidates, "flat")
                occupied = {(r["top"], r["left"]) for r in flats}
                rois = flats + select_regions([r for r in candidates if (r["top"], r["left"]) not in occupied], "detail", 4)
                if not rois:
                    raise ValueError(f"No valid regions: {path}")
                references = [read_patch(pixels, roi) for roi in rois]
                key = f"{engine}-{component}"
                if previews:
                    preview(pixels, rois, previews / f"{image_id}-{key}.png")
                group = {"baseline_path": str(path), "baseline_value": baseline_value,
                         "baseline_sha256": anchor.get("sha256"), "baseline_protocol_valid": bool(neutral and noise_neutral and neutral["protocol_valid"]),
                         "regions": rois, "baseline_metrics": [plane_metrics(p) for p in references], "variants": []}
                for entry in entries:
                    variant_file = Path(entry["path"])
                    before = variant_file.stat()
                    if (before.st_size, before.st_mtime_ns) != (entry["bytes"], entry["file_mtime_ns"]):
                        raise ValueError(f"Variant changed since metadata audit: {variant_file}")
                    variant = native_pixels(variant_file)
                    if variant.shape != pixels.shape:
                        raise ValueError(f"Within-engine geometry differs: {variant_file}")
                    measures = [{"roi": roi, **compare_patch(ref, read_patch(variant, roi))}
                                for roi, ref in zip(rois, references)]
                    if variant.shape[2] == 4:
                        opaque = all(np.all(variant[r["top"]:r["top"] + r["size"], r["left"]:r["left"] + r["size"], 3]
                                             == np.iinfo(variant.dtype).max) for r in rois)
                    else:
                        opaque = True
                    after = variant_file.stat()
                    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                        raise ValueError(f"File changed during reading: {variant_file}")
                    group["variants"].append({"path": str(variant_file), "value": entry["observed_value"], "declared_value": entry["value"],
                                               "extra_state": entry.get("extra_state", False),
                                               "file_mtime_ns": before.st_mtime_ns, "state_valid": entry["state_valid"],
                                               "protocol_valid": entry["protocol_valid"] and group["baseline_protocol_valid"],
                                               "opaque_on_regions": opaque, "summary": summarize(measures), "patches": measures})
                    print(f"Measured {image_id} {key} {entry['value']}", flush=True)
                after_anchor = path.stat()
                if (before_anchor.st_size, before_anchor.st_mtime_ns) != (after_anchor.st_size, after_anchor.st_mtime_ns):
                    raise ValueError(f"Reference changed during reading: {path}")
                groups[key] = group
        result["results"][image_id] = groups
    # Also guard already-measured files against replacement later in the campaign.
    for scene in result["scenes"].values():
        for entry in list(scene["neutrals"].values()) + scene["variants"]:
            if not entry.get("missing"):
                stat = Path(entry["path"]).stat()
                if (stat.st_size, stat.st_mtime_ns) != (entry["bytes"], entry["file_mtime_ns"]):
                    raise ValueError(f"Corpus changed during campaign; rerun after exports finish: {entry['path']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("tests/image/base"))
    parser.add_argument("--image-id", nargs="+", default=list(IDS))
    parser.add_argument("--analyse", action="store_true")
    parser.add_argument("--patch-size", type=int, default=256)
    parser.add_argument("--previews", type=Path, help="Diagnostic 8-bit overviews only; metrics always use native 16-bit regions")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant-file", nargs=5, action="append", default=[],
                        metavar=("IMAGE_ID", "COMPONENT", "ENGINE", "NUMBER", "PATH"),
                        help="Explicit mapping of exports with extra collision suffixes; LR XMP still audited")
    parser.add_argument("--neutral-file", nargs=3, action="append", default=[],
                        metavar=("IMAGE_ID", "ENGINE", "PATH"),
                        help="Explicit operator-declared neutral with a nonstandard filename; LR XMP still audited")
    parser.add_argument("--extra-state", nargs=5, action="append", default=[],
                        metavar=("IMAGE_ID", "COMPONENT", "ENGINE", "VALUE", "PATH"),
                        help="Explicit additional state (e.g. 1/5/10); standard 25/50/75/100 use --variant-file")
    args = parser.parse_args()
    if args.patch_size < 16:
        parser.error("--patch-size must be at least 16")
    if args.output.exists():
        parser.error("Refusing to overwrite an existing measurement; choose a new version")
    mappings = {}
    for image_id, component, engine, index, path in args.variant_file:
        if image_id not in args.image_id or component not in CONTROL or engine not in ("LR", "IPR") or index not in ("1", "2", "3", "4"):
            parser.error("Invalid --variant-file mapping")
        if not Path(path).is_file():
            parser.error(f"Mapped file missing: {path}")
        key = (image_id, component, engine, int(index))
        if key in mappings:
            parser.error("Duplicate --variant-file mapping")
        mappings[key] = Path(path)
    neutral_mappings = {}
    for image_id, engine, path in args.neutral_file:
        if image_id not in args.image_id or engine not in ("LR", "IPR"):
            parser.error("Invalid --neutral-file mapping")
        if not Path(path).is_file():
            parser.error(f"Mapped neutral missing: {path}")
        key = (image_id, engine)
        if key in neutral_mappings:
            parser.error("Duplicate --neutral-file mapping")
        neutral_mappings[key] = Path(path)
    extra_states, seen_extra = [], set()
    for image_id, component, engine, value_text, path in args.extra_state:
        if not value_text.isdigit() or not 1 <= int(value_text) <= 100:
            parser.error("Invalid --extra-state value (expected 1..100)")
        value = int(value_text)
        if image_id not in args.image_id or component not in CONTROL or engine not in ("LR", "IPR") or value in VALUES:
            parser.error("Invalid --extra-state mapping; use --variant-file for standard values")
        if not Path(path).is_file():
            parser.error(f"Extra state missing: {path}")
        key = (image_id, component, engine, value)
        if key in seen_extra:
            parser.error("Duplicate --extra-state mapping")
        seen_extra.add(key)
        extra_states.append({"image_id": image_id, "component": component, "engine": engine,
                             "value": value, "path": Path(path)})
    result = inventory(args.root, args.image_id, mappings, neutral_mappings, extra_states)
    result["analyser_sha256"] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    if args.analyse:
        if args.previews:
            args.previews.mkdir(parents=True, exist_ok=True)
        analyse(result, args.patch_size, args.previews)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
