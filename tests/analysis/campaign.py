"""Bounded-memory campaign execution, immutable provenance and explicit exclusions."""
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path
import queue
import platform
import sys
import json

import numpy as np
import PIL

from .audit import audit
from .catalog import SPATIAL, group_for, PROTOCOLS
from .groups import measure, measure_roi
from .metrics import detail, linearize, mean
from .native import encoded, inspect, sha256, verify_unchanged


def select_rois(pixels, size=256):
    """Fixed native candidates, ranked on neutral only; not semantic truth."""
    height, width = pixels.shape[:2]
    size = min(size, height, width)
    if size < 2:
        return []
    candidates = []
    positions = {(int(top), int(left)) for top in np.linspace(0, height - size, 4)
                 for left in np.linspace(0, width - size, 4)}
    for top, left in sorted(positions):
        rgb = linearize(encoded(pixels, [top, left, size, size]))
        y = rgb @ np.array([.2126, .7152, .0722])
        if .001 < y.mean() < .8 and (rgb.max(axis=-1) >= .999).mean() < .01:
            score = detail(rgb)["gradient_RMS"]
            candidates.append({"box": [top, left, size, size], "neutral_gradient_RMS": score})
    candidates.sort(key=lambda roi: roi["neutral_gradient_RMS"])
    flat = candidates[:min(6, len(candidates))]
    remaining = candidates[len(flat):]
    return [{**roi, "kind": "flat_candidate", "selection": "automatic_not_semantically_verified"} for roi in flat] + [
        {**roi, "kind": "detail_candidate", "selection": "automatic_not_semantically_verified"} for roi in remaining[-3:]]


def explicit_rois(rois, shape):
    result = []
    for roi in rois:
        box = roi.get("box")
        if (not isinstance(box, list) or len(box) != 4 or
                any(isinstance(v, bool) or not isinstance(v, int) for v in box)):
            raise ValueError("ROI box must be four integers: top,left,height,width")
        top, left, height, width = box
        if min(top, left) < 0 or min(height, width) < 2 or top + height > shape[0] or left + width > shape[1]:
            raise ValueError("ROI outside native image bounds")
        result.append({"box": box, "kind": roi.get("kind", "operator"), "selection": "explicit_mapping"})
    return result


def response_metric(group, record):
    feature, metrics = record["feature"], record.get("metrics", {})
    if group == "couleur":
        if feature == "temperature":
            return "delta_log_R_B", metrics.get("delta_log_R_B"), "increasing"
        if feature == "tint":
            return "delta_log_magenta_green", metrics.get("delta_log_magenta_green"), "increasing"
        if feature in ("vibrance", "saturation"):
            return "mean_chroma_delta_low", metrics.get("chroma_groups", {}).get("low_chroma", {}).get("mean_chroma_delta"), "increasing"
    if group == "melangeur":
        field = {"hue": "core_hue_delta_degrees", "saturation": "core_saturation_delta", "luminance": "core_Y_ratio"}[feature.split("-")[-1]]
        return field, metrics.get(field), "increasing"
    if group == "bruit":
        key = "sigma_Y_Haar" if feature == "denoise-luminance" else "sigma_chroma_Haar"
        values = [p["metrics"]["detail_ratios"][key] for p in record.get("rois", [])
                  if p["roi"]["kind"] in ("flat_candidate", "flat", "operator")]
        values = [v for v in values if v is not None]
        return key + "_ratio_ROI_median", float(np.median(values)) if values else None, "decreasing"
    if group == "lumiere" and feature in ("exposure", "shadows", "blacks", "highlights", "whites"):
        return "mean_Y_delta", metrics.get("mean_Y_delta"), "increasing"
    return None, None, None


def progressions(group, records):
    series = {}
    for record in records:
        if record["status"] == "excluded":
            continue
        field, value, direction = response_metric(group, record)
        if field is None:
            continue
        key = (record["scene"], record["engine"], record["feature"],
               record["reference_metadata"]["sha256"], json.dumps(record.get("support", []), sort_keys=True))
        item = series.setdefault(key, {"scene": key[0], "engine": key[1], "feature": key[2],
                                       "reference_sha256": key[3], "metric": field, "direction": direction,
                                       "points": [], "scope": "observed states only; no invented zero anchor"})
        item["points"].append({"value": record["value"], "response": value,
                               "protocol_usable": record["protocol_usable"]})
    for item in series.values():
        points = sorted(item["points"], key=lambda p: p["value"])
        item["points"] = points
        usable = (len(points) >= 2 and len({p["value"] for p in points}) == len(points)
                  and all(p["response"] is not None and p["protocol_usable"] for p in points))
        item["tolerance"] = .05 if "degrees" in item["metric"] else 1e-4
        sign = 1 if item["direction"] == "increasing" else -1
        item["monotonic_observed_means"] = (all(sign * (b["response"] - a["response"]) >= -item["tolerance"]
                                                    for a, b in zip(points, points[1:])) if usable else None)
        item["full_protocol_coverage_not_asserted"] = True
    return list(series.values())


RENDER_CONTEXT = ("source_sha256", "executable_sha256", "dcp_profile", "lcp_profile", "lcp_sha256",
                  "raw_normalization")


def measure_record(group, original, load, step, patch_size):
    record = dict(original)
    record.update(status="excluded", protocol_usable=False, issues=[], metrics=None, rois=[])
    try:
        if not record.get("reference"):
            raise ValueError("Missing explicit own-engine neutral/reference")
        baseline, metadata0, _ = load(record["reference"], record.get("reference_render"))
        variant, metadata1, _ = load(record["file"], record.get("render"))
        record.update(reference_metadata=metadata0, metadata=metadata1)
        if record.get("render"):
            declared, reference = metadata1["declaration"], metadata0["declaration"]
            if any(reference.get(k) != declared.get(k) for k in RENDER_CONTEXT):
                raise ValueError("IPR development context differs from reference")
            record.update(settings=declared["settings"], reference_settings=reference["settings"],
                          expected_sha256=metadata1["sha256"], reference_sha256=metadata0["sha256"])
            record["provenance"] = {**record["provenance"], "declaration": declared}
        if baseline.shape != variant.shape:
            raise ValueError("Within-engine geometry mismatch; annotated registration needed")
        for key_hash, metadata in (("expected_sha256", metadata1), ("reference_sha256", metadata0)):
            expected = record.get(key_hash)
            if expected and metadata["sha256"].lower() != expected.lower():
                raise ValueError("Pixel file hash differs from declared " + key_hash)
        if metadata0["icc_sha256"] != metadata1["icc_sha256"]:
            raise ValueError("Within-engine ICC profile changed")
        warnings, supports = audit(record, metadata1, metadata0)
        record["issues"].extend(warnings)
        record["support"] = supports
        record["metrics"] = measure(group, record, encoded(baseline, step=step), encoded(variant, step=step))
        record["sampling"] = {"global_step": step, "pixels_native_not_resized": True,
                              "global_not_exhaustive": step != 1}
        if group in SPATIAL:
            rois = (explicit_rois(record["rois_declared"], baseline.shape) if record.get("rois_declared")
                    else select_rois(baseline, patch_size))
            if not rois:
                record["issues"].append("no_admissible_native_ROI")
            for roi in rois:
                record["rois"].append({"roi": roi, "sampling_step": 1,
                                       "metrics": measure_roi(group, record, encoded(baseline, roi["box"]),
                                                              encoded(variant, roi["box"]))})
        record["protocol_usable"] = not record["issues"]
        record["status"] = "measured" if record["protocol_usable"] else "diagnostic"
    except (ValueError, OSError, KeyError, TypeError) as error:
        record["issues"].append(str(error))
        record["status"] = "excluded"
        record["protocol_usable"] = False
        record["metrics"] = None
        record["rois"] = []
    return record


def analyse(group, records, step=8, patch_size=256, data_version=1, supersedes=None, servers=()):
    """File states are measured in this thread; rendered states one source per
    render server, the servers working in parallel."""
    selected = [r for r in records if group_for(r["feature"]) == group]
    if not selected:
        raise ValueError("No mapped states for group " + group)
    if step < 1 or patch_size < 2 or data_version < 1:
        raise ValueError("Positive step/version and ROI size >= 2 required")
    seen = set()
    for record in selected:
        key = (record["scene"], record["engine"], record["feature"], record["value"], record["file"])
        if key in seen:
            raise ValueError("Duplicate state association: " + str(key))
        seen.add(key)
    cache = {}
    def load_file(path, render=None):
        if render is not None:
            raise ValueError("IPR state rendered in memory needs iprRenderServer")
        path = str(Path(path).resolve())
        if path not in cache:
            cache[path] = inspect(Path(path))
        return cache[path]
    measured = [None] * len(selected)
    by_source = {}
    for index, record in enumerate(selected):
        if record.get("render") and servers:
            by_source.setdefault(record["render"]["source"], []).append(index)
        else:
            measured[index] = measure_record(group, record, load_file, step, patch_size)
    idle = queue.Queue()
    for server in servers:
        idle.put(server)
    def measure_source(indices):
        # A source's references stay in memory while its variants are measured
        # and released (about 6 bytes per pixel each).
        server = idle.get()
        try:
            references, rendered = {selected[i]["reference"] for i in indices}, {}
            def load(path, render=None):
                if render is None:
                    return load_file(path)
                if path in rendered:
                    return rendered[path]
                pixels, metadata = server.render(render["source"], render["feature"], render["value"])
                if path in references:
                    rendered[path] = pixels, metadata, None
                return pixels, metadata, None
            for i in indices:
                measured[i] = measure_record(group, selected[i], load, step, patch_size)
        finally:
            idle.put(server)
    with ThreadPoolExecutor(max_workers=max(1, len(servers))) as pool:
        for future in [pool.submit(measure_source, indices) for indices in by_source.values()]:
            future.result()
    # Recheck full hashes at campaign close, including references, before publication.
    for path, (_, metadata, before) in cache.items():
        verify_unchanged(path, before, metadata["sha256"])
    # Rendered states: each source must still be the file the server decoded.
    declared_sources = {r["render"]["source"]: r["metadata"]["declaration"]["source_sha256"]
                        for r in measured if r.get("render") and r.get("metadata")}
    for source, digest in declared_sources.items():
        if sha256(source) != digest:
            raise ValueError("Source changed during campaign: " + source)
    now = datetime.now(timezone.utc)
    package = Path(__file__).parent
    counts = {status: sum(r["status"] == status for r in measured) for status in ("measured", "diagnostic", "excluded")}
    return {"campaign_id": f"mesure-{group}-v{data_version:03d}-{now.strftime('%Y%m%dT%H%M%S%fZ')}",
            "schema_version": 1, "data_version": data_version, "measured_at_utc": now.isoformat(),
            "supersedes_campaign_id": supersedes, "group": group, "protocol": PROTOCOLS[group],
            "protocol_version": "grouped-native-v2", "analysis_version": "2.0",
            "analyser_sources_sha256": {p.name: sha256(p) for p in sorted(package.glob("*.py"))},
            "software": {"python": sys.version, "numpy": np.__version__, "pillow": PIL.__version__, "platform": platform.platform()},
            "method": {"space": "linear sRGB D65", "global_step": step, "ROI_step": 1,
                       "ROI_size": patch_size, "clipping_encoded_threshold": 1 - 1 / 65535,
                       "black_linear_Y_threshold": .001, "geometry": "own-engine only; no cross-engine registration/MAE",
                       "RGB_to_Y": [.2126, .7152, .0722], "noise_proxy": "unit-norm HH Haar MAD / 0.6744897501960817"},
            "units": {"Y": "relative linear luminance", "RGB": "linear channel value 0..1",
                      "MAE": "mean absolute linear channel difference to own reference, not error to truth",
                      "chroma": "HSV saturation in linear RGB", "angle": "degrees", "axis": "natural log ratio",
                      "ratio": "dimensionless", "percent": "100 * count / samples", "samples": "native sampled pixels"},
            "ipr_pixels": ({"kind": "in_memory_render", "server": str(servers[0].executable),
                            "server_sha256": servers[0].sha256, "parallel_servers": len(servers)}
                           if servers else None),
            "records": measured, "coverage": counts, "progressions": progressions(group, measured),
            "decision": {"functional": "not_evaluated", "visual": "not_evaluated", "lightroom_parity": "not_evaluated",
                         "physical_accuracy": "not_evaluated", "cleanup_eligible": False},
            "limitations": ["Successful metrics do not imply acceptance or full protocol coverage",
                            "Profile labels are not cryptographic DCP/LCP identities",
                            "Automatic ROI candidates require visual/semantic verification",
                            "Comparison across versions needs compatible profiles, WB, optics, metrics and scene states"]}
