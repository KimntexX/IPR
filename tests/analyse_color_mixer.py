#!/usr/bin/env python3
"""Audit and compare photographic HSL TIFF exports (no source file mutation).

Pillow reads TIFF metadata; NumPy maps contiguous uncompressed 8/16-bit pixels
without an intermediate 8-bit conversion. Run from the repository root.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

import numpy as np
from PIL import Image, ImageCms

ROOT = Path("tests/image/base")
CENTERS = {"red": 0., "orange": 30., "yellow": 60., "green": 120.,
           "cyan": 180., "blue": 240., "violet": 270., "magenta": 300.}
LEGACY_CENTERS = {band: center for band, center in CENTERS.items() if band != "violet"}
ANALYSIS_CENTERS = CENTERS
COMPONENTS = ("hue", "saturation", "luminance")
LEVELS = {"extremes": (-100, 100), "intermediate": (-50, -25, 25, 50)}
CRS = "http://ns.adobe.com/camera-raw-settings/1.0/"
VARIANT_OVERRIDES = {}


def support_widths(band, version):
    """Match the explicitly declared export engine, not today's source code."""
    centers = CENTERS if version == "eight" else LEGACY_CENTERS
    if band not in centers:
        return 30., 30.  # Legacy violet: observation mask, not an IPR support.
    names, values = list(centers), list(centers.values())
    index = names.index(band)
    center = values[index]
    return (abs(float(circular_delta(center, values[(index - 1) % len(values)]))),
            abs(float(circular_delta(values[(index + 1) % len(values)], center))))


def band_identified(band, result):
    return band != "violet" or result.get("ipr_mixer_version", "legacy-seven") == "eight"


def metadata(path):
    with Image.open(path) as image:
        tags = image.tag_v2
        icc = image.info.get("icc_profile", b"")
        description = "missing"
        if icc:
            try:
                description = ImageCms.getProfileDescription(
                    ImageCms.ImageCmsProfile(io.BytesIO(icc))).strip()
            except Exception as error:
                description = "unreadable: " + str(error)
        xmp = tags.get(700, b"")
        if isinstance(xmp, tuple):
            xmp = bytes(xmp)
        settings = {}
        if xmp:
            try:
                for element in ET.fromstring(xmp).iter():
                    for key, value in element.attrib.items():
                        if key.startswith("{" + CRS + "}"):
                            settings[key.split("}")[1]] = value
            except (ET.ParseError, ValueError):
                settings["_parse_error"] = "invalid embedded XMP"
        active_mixer = {key: value for key, value in settings.items()
                        if "Adjustment" in key and
                        key.startswith(("Hue", "Saturation", "Luminance"))
                        and float(value) != 0}
        return {"path": str(path), "bytes": path.stat().st_size,
                "size": list(image.size), "bits": list(tags.get(258, ())),
                "samples": tags.get(277), "orientation": tags.get(274, 1),
                "compression": tags.get(259), "photometric": tags.get(262),
                "icc": description,
                "icc_sha256": hashlib.sha256(icc).hexdigest() if icc else None,
                "active_mixer": active_mixer, "settings": settings}


def variant_path(band, number, engine, image_id):
    if (band, number, engine) in VARIANT_OVERRIDES:
        return VARIANT_OVERRIDES[band, number, engine]
    folder = ROOT / "color-mixer" / band
    suffix = "" if number == 1 else f"-{number}"
    return folder / (f"{image_id}{suffix}.tif" if engine == "LR"
                     else f"{image_id}-IPR{suffix}.tiff")


def variant_state(number, mode="extremes"):
    values = LEVELS[mode]
    if not 1 <= number <= len(COMPONENTS) * len(values):
        raise ValueError(f"Invalid variant number {number} for {mode}")
    return COMPONENTS[(number - 1) // len(values)], values[(number - 1) % len(values)]


def neutral_path(image_id, engine):
    filename = f"{image_id}.tif" if engine == "LR" else f"{image_id}-IPR.tiff"
    path = ROOT / filename
    return path if path.exists() else ROOT / "neutre" / filename


def inventory(bands, image_id, mode="extremes"):
    result = {"reference": metadata(neutral_path(image_id, "LR")), "bands": {},
              "validation_mode": mode}
    for band in bands:
        entries = []
        expected = set()
        for engine in ("LR", "IPR"):
            for number in range(1, 1 + len(COMPONENTS) * len(LEVELS[mode])):
                path = variant_path(band, number, engine, image_id)
                expected.add(path.name)
                entries.append({"engine": engine, "number": number,
                                **(metadata(path) if path.exists()
                                   else {"path": str(path), "missing": True})})
        result["bands"][band] = {"entries": entries,
            "unexpected": [str(p) for p in sorted((ROOT / "color-mixer" / band).glob("*.tif*"))
                           if p.name.startswith(image_id) and p.name not in expected]}
    return result


def native_pixels(path):
    """Only accept the simple layout actually present in this corpus."""
    with Image.open(path) as image:
        tags = image.tag_v2
        width, height = image.size
        samples = tags.get(277, 3)
        bits = tuple(tags.get(258, ()))
        offsets, counts = tuple(tags.get(273, ())), tuple(tags.get(279, ()))
        if (tags.get(259, 1) != 1 or tags.get(284, 1) != 1 or
                tags.get(262) != 2 or tags.get(274, 1) != 1 or
                samples not in (3, 4) or bits not in ((8,) * samples, (16,) * samples) or
                tags.get(339, (1,) * samples) not in (1, (1,) * samples)):
            raise ValueError(f"Unsupported TIFF pixel layout: {path}")
        if not offsets or len(offsets) != len(counts) or any(
                offsets[i + 1] != offsets[i] + counts[i] for i in range(len(offsets) - 1)):
            raise ValueError(f"Non-contiguous TIFF strips: {path}")
        expected = width * height * samples * (bits[0] // 8)
        if sum(counts) != expected or offsets[0] + expected > path.stat().st_size:
            raise ValueError(f"Incomplete TIFF pixel data: {path}")
    with path.open("rb") as source:
        byte_order = source.read(2)
    if byte_order not in (b"II", b"MM"):
        raise ValueError(f"Invalid TIFF byte order: {path}")
    dtype = np.dtype(("<" if byte_order == b"II" else ">") + f"u{bits[0] // 8}")
    return np.memmap(path, mode="r", dtype=dtype, offset=offsets[0],
                     shape=(height, width, samples))


def sample(pixels, step, height, width, top=0, left=0):
    # Real pixels only: no resize, interpolation or synthetic padding.
    values = np.array(pixels[top:top + height:step, left:left + width:step, :3], dtype=np.float32)
    return values / np.iinfo(pixels.dtype).max


def linearize(encoded):
    return np.where(encoded <= .04045, encoded / 12.92, ((encoded + .055) / 1.055) ** 2.4)


def hsv(rgb):
    maximum, minimum = rgb.max(axis=2), rgb.min(axis=2)
    chroma = maximum - minimum
    safe = np.maximum(chroma, 1e-8)
    red, green, blue = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    hue = np.where(maximum == red, ((green - blue) / safe) % 6.,
                   np.where(maximum == green, (blue - red) / safe + 2.,
                            (red - green) / safe + 4.)) * 60.
    return np.where(chroma > 1e-8, hue, 0.), chroma / np.maximum(maximum, 1e-8), maximum


def luminance(rgb):
    return rgb @ np.array((.2126, .7152, .0722), dtype=np.float32)


def circular_delta(first, second):
    return (first - second + 180.) % 360. - 180.


def lab(rgb):
    xyz = rgb @ np.array(((.4124564, .2126729, .0193339),
                          (.3575761, .7151522, .1191920),
                          (.1804375, .0721750, .9503041)), dtype=np.float32)
    xyz /= np.array((.95047, 1., 1.08883), dtype=np.float32)
    epsilon, kappa = 216 / 24389, 24389 / 27
    f = np.where(xyz > epsilon, np.cbrt(xyz), (kappa * xyz + 16.) / 116.)
    return np.stack((116. * f[..., 1] - 16., 500. * (f[..., 0] - f[..., 1]),
                     200. * (f[..., 1] - f[..., 2])), axis=2)


def average(values, mask):
    return float(values[mask].mean()) if mask.any() else None


def edge_signature(encoded):
    y = luminance(linearize(encoded))
    dy, dx = np.gradient(y)
    edges = np.sqrt(dx * dx + dy * dy)
    return edges / max(float(np.sqrt(np.mean(edges * edges))), 1e-8)


def choose_crop(reference, target, step):
    height, width = target.shape[:2]
    dh, dw = reference.shape[0] - height, reference.shape[1] - width
    if dh not in (0, 1) or dw not in (0, 1):
        raise ValueError("Unexpected dimension mismatch: cannot compare without geometric registration")
    target_edges = edge_signature(sample(target, step, height, width))
    candidates = []
    for top in range(dh + 1):
        for left in range(dw + 1):
            edges = edge_signature(sample(reference, step, height, width, top, left))
            candidates.append({"top": top, "left": left,
                               "edge_mae": float(np.abs(edges - target_edges).mean())})
    return sorted(candidates, key=lambda entry: entry["edge_mae"])


def expected_setting(band, number, mode="extremes"):
    component, value = variant_state(number, mode)
    color = {"cyan": "Aqua", "violet": "Purple"}.get(band, band.capitalize())
    return {component.capitalize() + "Adjustment" + color: value}


def progression(records, component, engine):
    """Progression of spatial means relative to each engine's own neutral.

    This does not imply pixelwise monotonicity, linearity or Lightroom parity.
    A zero anchor is usable only when a neutral-relative metric was measured.
    """
    field, tolerance = {"hue": ("core_hue_delta", .05),
                        "saturation": ("core_saturation_delta", .0001),
                        "luminance": ("core_Y_ratio", .0001)}[component]
    selected = [r for r in records if r["component"] == component and r["engine"] == engine]
    points = [{"value": r["value"], "response": r.get(field),
               "usable": r.get("metadata_valid", True)} for r in selected]
    if points and all(p["response"] is not None for p in points):
        points.append({"value": 0, "response": 1. if component == "luminance" else 0., "usable": True})
    points.sort(key=lambda p: p["value"])
    usable = len(points) == 5 and all(p["usable"] and p["response"] is not None for p in points)
    steps = [{"from": a["value"], "to": b["value"], "delta": b["response"] - a["response"]}
             for a, b in zip(points, points[1:]) if a["response"] is not None and b["response"] is not None]
    return {"metric": field, "points": points, "steps": steps, "usable": usable,
            "tolerance": tolerance,
            "monotonic_mean": all(s["delta"] >= -tolerance for s in steps) if usable else None,
            "strictly_increasing_mean": all(s["delta"] > tolerance for s in steps) if usable else None}


def analyse(args, result):
    mode = result.get("validation_mode", "extremes")
    values = LEVELS[mode]
    numbers = range(1, 1 + len(COMPONENTS) * len(values))
    base_path = neutral_path(args.image_id, "LR")
    base_pixels = native_pixels(base_path)
    ipr_base_path = neutral_path(args.image_id, "IPR")
    # Prefer a neutral geometry anchor; allow LR-only or incomplete series.
    use_ipr_reference = ipr_base_path.exists() and not args.without_ipr_reference
    probe_path = ipr_base_path if use_ipr_reference else next(
        (path for band in args.bands for number in numbers
         if (path := variant_path(band, number, "IPR", args.image_id)).exists()), base_path)
    probe_pixels = native_pixels(probe_path)
    height, width = probe_pixels.shape[:2]
    step = max(1, int(np.ceil(max(height, width) / args.sample_long_edge)))
    crops = choose_crop(base_pixels, probe_pixels, step)
    chosen = crops[0]
    reference_encoded = sample(base_pixels, step, height, width, chosen["top"], chosen["left"])
    reference = linearize(reference_encoded)
    base_hue, base_saturation, _ = hsv(reference)
    base_y = luminance(reference)
    eligible = (base_saturation >= .15) & (base_y > .01) & (base_y < .9)
    ipr_base = None
    if use_ipr_reference:
        pixels = native_pixels(ipr_base_path)
        if pixels.shape[:2] != (height, width):
            raise ValueError("IPR neutral dimensions differ from IPR variants")
        result["ipr_reference"] = metadata(ipr_base_path)
        ipr_base = linearize(sample(pixels, step, height, width))
    result["measurement"] = {"utc": datetime.now(timezone.utc).isoformat(),
        "step": step, "sample_size": list(reference.shape[:2]),
        "sample_pixels": int(reference.shape[0] * reference.shape[1]),
        "geometry_anchor": str(probe_path), "analysis_version": 4,
        "validation_mode": mode, "command_values": list(values),
        "ipr_mixer_version": result.get("ipr_mixer_version", "legacy-seven"),
        "crop_candidates": crops, "chosen_crop": chosen,
        "ipr_neutral_available": ipr_base is not None,
        "ipr_neutral_explicitly_excluded": args.without_ipr_reference,
        "precision": "native unsigned 16-bit/8-bit RGB -> float32; no 8-bit conversion or resizing",
        "limitations": ["One photographic scene, not a calibrated chart",
                        "Crop selected by sampled luminance-edge discrepancy, not full registration",
                        "IPR input mask cannot be recovered exactly from output sRGB TIFFs",
                        "Delta E is CIE76, D65; it is not CIEDE2000"]}
    if ipr_base is not None:
        base_de = np.linalg.norm(lab(ipr_base) - lab(reference), axis=2)
        result["measurement"]["neutral_comparison"] = {
            "MAE_linear": float(np.abs(ipr_base - reference).mean()),
            "DeltaE76_mean": float(base_de.mean()),
            "LR_mean_Y": float(base_y.mean()), "IPR_mean_Y": float(luminance(ipr_base).mean())}
    result["results"] = {}
    for band in args.bands:
        center = ANALYSIS_CENTERS[band]
        distance = np.abs(circular_delta(base_hue, center))
        core = eligible & (distance <= 15.)
        signed = circular_delta(base_hue, center)
        before, after = support_widths(band, result.get("ipr_mixer_version", "legacy-seven"))
        outside = eligible & ((signed < -before) | (signed > after))
        group = {"core_pixels": int(core.sum()), "core_percent": 100. * float(core.mean()),
                 "outside_pixels": int(outside.sum()), "variants": [], "pairs": {},
                 "analysis_center_degrees": center,
                 "missing_files": [str(variant_path(band, n, engine, args.image_id))
                                   for engine in ("LR", "IPR") for n in numbers
                                   if not variant_path(band, n, engine, args.image_id).exists()],
                 "comparison_kind": "non-equivalent or unidentified IPR band" if not band_identified(band, result)
                                    else "same nominal band (IPR naming assumed from operator)"}
        variants = {}
        for number in numbers:
            pair_data = {}
            for engine in ("LR", "IPR"):
                path = variant_path(band, number, engine, args.image_id)
                if not path.exists():
                    continue
                before_stat = path.stat()
                pixels = native_pixels(path)
                if engine == "LR":
                    if pixels.shape[:2] != base_pixels.shape[:2]:
                        raise ValueError(f"LR variant dimensions differ from its neutral: {path}")
                    encoded = sample(pixels, step, height, width, chosen["top"], chosen["left"])
                else:
                    if pixels.shape[:2] != (height, width):
                        raise ValueError(f"IPR variant dimensions differ: {path}")
                    encoded = sample(pixels, step, height, width)
                after_stat = path.stat()
                if (before_stat.st_size, before_stat.st_mtime_ns) != (after_stat.st_size, after_stat.st_mtime_ns):
                    raise ValueError(f"File changed while reading: {path}")
                rgb = linearize(encoded)
                h, s, _ = hsv(rgb)
                y = luminance(rgb)
                record = {"engine": engine, "number": number,
                    "file_mtime_ns": before_stat.st_mtime_ns,
                    "component": variant_state(number, mode)[0], "value": variant_state(number, mode)[1],
                    "core_mean_Y": average(y, core), "core_mean_saturation": average(s, core),
                    "clipped_channel_percent": 100. * float((encoded >= 1. - 1./65535.).any(axis=2).mean()),
                    "black_pixel_percent": 100. * float((encoded <= 1./65535.).all(axis=2).mean())}
                if pixels.shape[2] == 4:
                    alpha = pixels[::step, ::step, 3]
                    record["nonopaque_alpha_percent"] = 100. * float(
                        (alpha != np.iinfo(pixels.dtype).max).mean())
                baseline = reference if engine == "LR" else ipr_base
                if baseline is not None:
                    bh, bs, _ = hsv(baseline)
                    by = luminance(baseline)
                    hue_valid = core & (s > .05) & (bs > .05)
                    record.update({"core_hue_delta": average(circular_delta(h, bh), hue_valid),
                        "core_saturation_delta": average(s - bs, core),
                        "core_Y_ratio": float(y[core].sum() / max(float(by[core].sum()), 1e-8)) if core.any() else None,
                        "outside_MAE_linear": average(np.abs(rgb - baseline).mean(axis=2), outside)})
                if engine == "LR":
                    observed = metadata(path)["active_mixer"]
                    observed = {key: int(float(value)) for key, value in observed.items()}
                    record["metadata_valid"] = observed == expected_setting(band, number, mode)
                    if not record["metadata_valid"]:
                        record["observed_settings"] = observed
                pair_data[engine] = (encoded, rgb, h, s, y, record)
                group["variants"].append(record)
            if "LR" in pair_data and "IPR" in pair_data:
                lr_e, lr, _, _, _, lr_record = pair_data["LR"]
                ipr_e, ipr, _, _, _, _ = pair_data["IPR"]
                de = np.linalg.norm(lab(ipr) - lab(lr), axis=2)
                group["variants"][-1]["comparison"] = {
                    "usable": lr_record.get("metadata_valid", False) and band_identified(band, result),
                    "MAE_encoded": float(np.abs(ipr_e - lr_e).mean()),
                    "MAE_linear": float(np.abs(ipr - lr).mean()),
                    "core_MAE_linear": average(np.abs(ipr - lr).mean(axis=2), core),
                    "core_DeltaE76_mean": average(de, core),
                    "DeltaE76_mean": float(de.mean())}
                if ipr_base is not None:
                    effect_error = np.abs((ipr - ipr_base) - (lr - reference)).mean(axis=2)
                    group["variants"][-1]["comparison"].update({
                        "effect_MAE_linear": float(effect_error.mean()),
                        "core_effect_MAE_linear": average(effect_error, core)})
            # Encoded arrays are needed only for the immediate LR/IPR comparison.
            variants[number] = {engine: (None, *data[1:]) for engine, data in pair_data.items()}
            print(f"Measured {band} {number}/{len(numbers)}", flush=True)
        for index, component in enumerate(COMPONENTS):
            low, high = len(values) * index + 1, len(values) * (index + 1)
            endpoints = {}
            for engine in ("LR", "IPR"):
                if engine not in variants[low] or engine not in variants[high]:
                    continue
                _, minus, mh, ms, my, mr = variants[low][engine]
                _, plus, ph, ps, py, pr = variants[high][engine]
                valid_hue = core & (ms > .05) & (ps > .05)
                hue_change = circular_delta(ph, mh)
                endpoints[engine] = {
                    "usable": engine == "IPR" or (mr["metadata_valid"] and pr["metadata_valid"]),
                    "core_hue_plus_minus": average(hue_change, valid_hue),
                    "hue_positive_fraction": average((hue_change > .1).astype(float), valid_hue),
                    "core_saturation_plus_minus": average(ps - ms, core),
                    "core_Y_plus_minus": average(py - my, core),
                    "saturation_positive_fraction": average((ps > ms + .001).astype(float), core),
                    "Y_positive_fraction": average((py > my + .001).astype(float), core),
                    "outside_pair_MAE_linear": average(np.abs(plus - minus).mean(axis=2), outside),
                    "endpoints_identical_on_sample": bool(np.array_equal(plus, minus))}
            group["pairs"][component] = endpoints
        if mode == "intermediate":
            group["progression"] = {component: {engine: progression(group["variants"], component, engine)
                                                for engine in ("LR", "IPR")}
                                    for component in COMPONENTS}
        result["results"][band] = group
    result["audit"] = audit(result)


def audit(result):
    """Do not silently label wrong states/baselines as a valid comparison."""
    issues = []
    if result["reference"]["active_mixer"]:
        issues.append({"type": "non_neutral_reference", "path": result["reference"]["path"],
                       "settings": result["reference"]["active_mixer"]})
    for band, inventory_group in result["bands"].items():
        for entry in inventory_group["entries"]:
            if entry.get("missing"):
                issues.append({"type": "missing_variant", "path": entry["path"]})
                continue
            if "srgb" not in entry["icc"].lower():
                raise ValueError(f"Cannot apply analytic sRGB decoding to {entry['icc']}: {entry['path']}")
            if entry["engine"] == "LR":
                observed = {k: int(float(v)) for k, v in entry["active_mixer"].items()}
                if observed != expected_setting(band, entry["number"], result.get("validation_mode", "extremes")):
                    issues.append({"type": "wrong_variant_settings", "path": entry["path"],
                                   "observed": observed})
                differences = {key: {"neutral": result["reference"]["settings"].get(key), "variant": value}
                               for key, value in entry["settings"].items()
                               if not key.startswith(("HueAdjustment", "SaturationAdjustment", "LuminanceAdjustment"))
                               and result["reference"]["settings"].get(key) != value}
                if differences:
                    issues.append({"type": "non_mixer_settings_differ", "path": entry["path"],
                                   "differences": differences})
        if not band_identified(band, result):
            issues.append({"type": "band_identity_mismatch", "band": band,
                           "note": "Legacy IPR exports have no independent Purple band; do not claim parity"})
    for key in ("reference", "ipr_reference"):
        if key in result and "srgb" not in result[key]["icc"].lower():
            raise ValueError(f"Cannot decode non-sRGB neutral: {result[key]['path']}")
    return issues


def main():
    global ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT,
                        help="Corpus base directory (including neutral TIFFs and color-mixer/); supports archived campaigns")
    parser.add_argument("bands", nargs="*")
    parser.add_argument("--image-id", default="6V1A0327")
    parser.add_argument("--levels", choices=tuple(LEVELS), default="extremes",
                        help="extremes: 6 states at +/-100; intermediate: 12 states, H/S/L each -50,-25,+25,+50")
    parser.add_argument("--ipr-mixer-version", choices=("legacy-seven", "eight"),
                        default="legacy-seven",
                        help="Operator-declared engine: use eight only for freshly regenerated eight-band exports")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--analyse", action="store_true")
    parser.add_argument("--without-ipr-reference", action="store_true",
                        help="Diagnostic pairs only: exclude the default IPR neutral; no IPR neutral-relative metrics")
    parser.add_argument("--sample-long-edge", type=int, default=1392)
    parser.add_argument("--inspect-file", type=Path)
    parser.add_argument("--variant-file", nargs=4, action="append", default=[],
                        metavar=("BAND", "ENGINE", "NUMBER", "PATH"),
                        help="Explicit file mapping; embedded LR settings are still audited")
    args = parser.parse_args()
    ROOT = args.root
    args.bands = args.bands or ["red", "yellow", "green"]
    if set(args.bands) - set(ANALYSIS_CENTERS):
        parser.error("Unsupported colour group")
    for band, engine, number, path in args.variant_file:
        if band not in args.bands or engine not in ("LR", "IPR") or number not in map(str, range(1, 1 + len(COMPONENTS) * len(LEVELS[args.levels]))):
            parser.error("Invalid --variant-file mapping")
        if not Path(path).is_file():
            parser.error(f"Mapped variant does not exist: {path}")
        VARIANT_OVERRIDES[band, int(number), engine] = Path(path)
    if args.inspect_file:
        result = metadata(args.inspect_file)
        content = json.dumps(result, indent=2, ensure_ascii=False)
        if args.output:
            args.output.write_text(content + "\n", encoding="utf-8")
        else:
            print(content)
        return
    result = inventory(args.bands, args.image_id, args.levels)
    result["ipr_mixer_version"] = args.ipr_mixer_version
    result["mapping_overrides"] = [
        {"band": band, "engine": engine, "number": int(number), "path": path}
        for band, engine, number, path in args.variant_file]
    # Audit before measurement, so unsupported colour spaces never produce misleading metrics.
    result["audit"] = audit(result)
    if args.analyse:
        if args.sample_long_edge < 100:
            parser.error("--sample-long-edge must be at least 100")
        analyse(args, result)
    content = json.dumps(result, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(content + "\n", encoding="utf-8")
    else:
        print(content)


if __name__ == "__main__":
    main()
