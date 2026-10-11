"""Explicit state associations, generator manifests and optional LR mappings."""
import math
from pathlib import Path

from .catalog import group_for, bounds_for
from .native import local_path, read_json, sha256


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


def manifest_records(path, image_root=None):
    path = local_path(path)
    data = read_json(path)
    if data.get("status") != "complete" or data.get("format") != "TIFF" or data.get("bitDepth") != 16:
        raise ValueError("Need complete TIFF 16-bit generator manifest: " + str(path))
    exports = data.get("exports", [])
    if data.get("expectedExports") != len(exports) or not exports:
        raise ValueError("Incomplete export count: " + str(path))
    arguments = data.get("arguments", [])
    root = image_root
    if root is None and "--output" in arguments:
        root = arguments[arguments.index("--output") + 1]
    root = local_path(root) if root is not None else path.parent
    neutrals = {}
    for entry in exports:
        if entry.get("feature") == "neutral":
            source = entry["source"]
            if source in neutrals:
                raise ValueError("Duplicate neutral source in manifest")
            neutrals[source] = entry
    records = []
    for entry in exports:
        source = entry["source"]
        neutral = neutrals.get(source)
        if neutral is None:
            raise ValueError("Manifest source without neutral: " + source)
        relative = Path(entry["file"].replace("\\", "/"))
        if relative.is_absolute() or ".." in relative.parts or ":" in str(relative):
            raise ValueError("Export path must stay relative to image root")
        record = {"scene": Path(source.replace("\\", "/")).stem, "engine": "IPR",
                  "feature": entry["feature"], "value": entry["value"],
                  "file": str(local_path(entry["file"], root)),
                  "reference": str(local_path(neutral["file"], root)),
                  "expected_sha256": entry.get("sha256"),
                  "reference_sha256": neutral.get("sha256"),
                  "settings": entry.get("settings"), "reference_settings": neutral.get("settings"),
                  "provenance": {"kind": "generator_manifest", "manifest": str(path),
                                 "manifest_sha256": sha256(path), "source": source,
                                 "source_sha256": entry.get("sourceSha256"),
                                 "ipr_version": data.get("iprVersion"),
                                 "executable_sha256": data.get("executableSha256"),
                                 "qt_version": data.get("qtVersion"),
                                 "dcp_profile": entry.get("dcpProfile"),
                                 "lcp_profile": entry.get("lcpProfile"),
                                 "reference_temperature_kelvin": entry.get("referenceTemperatureKelvin"),
                                 "camera_neutral_required": data.get("cameraNeutralRequired"),
                                 "normalization_environment": data.get("normalizationEnvironment"),
                                 "profile_hashes": None}}
        validate_record(record)
        records.append(record)
    return records


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
