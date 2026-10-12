"""Current embedded TIFF declarations and explicit operator associations."""
import math
from pathlib import Path

from .catalog import group_for, bounds_for
from .native import local_path, read_json, sha256, embedded_metadata
from .catalog import GROUPS
from .audit import field_for, finite_number


def feature_folder(feature):
    if feature in ("temperature", "tint", "vibrance", "saturation", "grayscale"):
        return "couleur/" + {"tint": "nuance", "grayscale": "niveaux_gris"}.get(feature, feature)
    if feature in ("texture", "clarity", "dehaze"):
        return {"clarity": "clarte", "dehaze": "correction_de_voile"}.get(feature, feature)
    if feature.startswith("mixer-"):
        return "color-mixer/" + feature.split("-")[1]
    if feature.startswith("grading-"):
        return "grading/" + feature.split("-")[1]
    if feature.startswith("denoise-"):
        return "bruit/" + ("luminance" if feature.endswith("luminance") else "couleur")
    return group_for(feature) + "/" + feature


def base_records(base, groups):
    """Exact current layout. LR numeric suffixes do not declare a state."""
    base = local_path(base)
    if not base.is_dir():
        raise ValueError("Missing base directory")
    records, cache, seen = [], {}, set()
    def metadata(path):
        path = local_path(path)
        if not path.is_relative_to(base):
            raise ValueError("Image/reference escapes base: " + str(path))
        if path not in cache:
            cache[path] = embedded_metadata(path)
        return cache[path]
    def tiffs(folder):
        return sorted(p for p in folder.iterdir() if p.is_file() and p.suffix.lower() in (".tif", ".tiff")) if folder.is_dir() else []
    neutral_candidates = tiffs(base / "neutre")
    for group in groups:
        for feature in GROUPS[group]:
            if feature == "neutral":
                continue
            folder = base / feature_folder(feature)
            variants = tiffs(folder)
            for path in variants:
                declaration, xmp = metadata(path)
                if declaration:
                    if declaration.get("feature") != feature:
                        continue  # Mixer bands share a directory, not a control.
                    reference_name = declaration.get("reference", "")
                    if not reference_name or Path(reference_name).is_absolute() or ".." in Path(reference_name).parts:
                        raise ValueError("Invalid embedded reference path: " + str(path))
                    reference = local_path(reference_name, base)
                    ref, _ = metadata(reference)
                    if not ref or ref.get("scene") != declaration.get("scene"):
                        raise ValueError("Missing/mismatched embedded IPR reference: " + str(path))
                    context_keys = ("source_sha256", "executable_sha256", "dcp_profile", "lcp_profile", "lcp_sha256", "raw_normalization")
                    if any(ref.get(k) != declaration.get(k) for k in context_keys):
                        raise ValueError("IPR development context differs from reference: " + str(path))
                    record = {"scene": declaration["scene"], "engine": "IPR", "feature": feature,
                              "value": declaration["value"], "file": str(path.resolve()), "reference": str(reference),
                              "settings": declaration.get("settings"), "reference_settings": ref.get("settings"),
                              "expected_sha256": sha256(path), "reference_sha256": sha256(reference),
                              "provenance": {"kind": "embedded_tiff", "declaration": declaration}}
                    if feature in ("temperature", "tint", "sharpening-radius", "sharpening-masking") or feature.startswith("grading-"):
                        record["reference_role"] = "support"
                else:
                    # No declaration is never interpreted as IPR based on a filename.
                    if "-IPR" in path.stem:
                        raise ValueError("IPR TIFF lacks current embedded declaration; regenerate or use --mapping: " + str(path))
                    if not xmp:
                        raise ValueError("TIFF lacks auditable LR XMP; use --mapping: " + str(path))
                        continue
                    field = field_for(feature)
                    value = finite_number(xmp.get(field)) if field else None
                    if value is None:
                        raise ValueError("No XMP control audit for " + feature + "; use --mapping: " + str(path))
                    scene = path.stem.split("-")[0]
                    anchor = 5200 if feature == "temperature" else 0
                    candidates = variants if feature in ("temperature", "tint") else neutral_candidates
                    matches = []
                    ignored = {field, "HasSettings", "Version", "UUID", "Digest"}
                    for candidate in candidates:
                        if candidate.stem.split("-")[0] != scene:
                            continue
                        ipr, baseline = metadata(candidate)
                        if ipr or not baseline or "-IPR" in candidate.stem:
                            continue
                        if feature in ("temperature", "tint") and finite_number(baseline.get(field)) != anchor:
                            continue
                        if all(xmp.get(k) == baseline.get(k) for k in (xmp.keys() | baseline.keys()) - ignored):
                            matches.append(candidate)
                    unique = {sha256(p): p for p in matches}
                    if len(unique) > 1:
                        raise ValueError("Ambiguous LR reference: " + str(path))
                    record = {"scene": scene, "engine": "LR", "feature": feature, "value": value,
                              "file": str(path.resolve()), "provenance": {"kind": "embedded_lr_xmp"}}
                    if unique:
                        record["reference"] = str(next(iter(unique.values())).resolve())
                    if feature in ("temperature", "tint"):
                        record["reference_role"] = "support"
                validate_record(record)
                key = (record["engine"], record["scene"], feature, record["value"])
                if key in seen:
                    raise ValueError("Duplicate current state: " + str(key))
                seen.add(key)
                records.append(record)
    return records


def validate_record(record):
    for key in ("scene", "engine", "feature", "file"):
        if not isinstance(record.get(key), str) or not record[key]:
            raise ValueError("Record needs nonempty " + key)
    if record["engine"] not in ("IPR", "LR"):
        raise ValueError("Engine must be IPR or LR")
    group_for(record["feature"])
    value = record.get("value")
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError("State value must be finite numeric")
    low, high = bounds_for(record["feature"])
    if not low <= value <= high:
        raise ValueError("State outside feature range: " + record["feature"])
    if record["feature"] not in ("exposure", "temperature", "sharpening-radius") and value != int(value):
        raise ValueError("Integer control requires integer state")




def mapping_records(path):
    path = local_path(path)
    data = read_json(path)
    if data.get("schema_version") != 1 or not isinstance(data.get("records"), list):
        raise ValueError("Mapping requires schema_version 1 and records array")
    root = local_path(data.get("image_root", "."), path.parent)
    records = []
    for entry in data["records"]:
        record = dict(entry)
        validate_record(record)
        record["file"] = str(local_path(record["file"], root))
        if record.get("reference"):
            record["reference"] = str(local_path(record["reference"], root))
        record["provenance"] = {**record.get("provenance", {}),
                                "kind": "explicit_mapping", "mapping": str(path),
                                "mapping_sha256": sha256(path)}
        records.append(record)
    neutrals = {}
    for record in records:
        if record["feature"] == "neutral":
            key = record["scene"], record["engine"]
            if key in neutrals:
                raise ValueError("Ambiguous mapped neutral for " + str(key))
            neutrals[key] = record
    for record in records:
        if not record.get("reference"):
            neutral = neutrals.get((record["scene"], record["engine"]))
            if neutral:
                record["reference"] = neutral["file"]
                record["reference_settings"] = neutral.get("settings")
                record["reference_sha256"] = neutral.get("expected_sha256")
    return records
