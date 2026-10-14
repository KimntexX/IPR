"""Metadata-audited global colour responses using the canonical group metrics.

Reads TIFFs only. No source render, image deletion, implicit LR index mapping,
cross-engine registration or claim of visual acceptance.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import subprocess

import numpy as np

from analysis import audit, campaign, native

FEATURES = ("vibrance", "saturation")
LEVELS = (-100, -50, -25, 0, 25, 50, 100)


def tiffs(folder):
    return sorted(p.resolve() for p in folder.iterdir()
                  if p.is_file() and p.suffix.lower() in (".tif", ".tiff")) if folder.is_dir() else []


def associations(base):
    base = base.resolve()
    entries, records, pending = [], [], []
    snapshots = {}
    for feature in FEATURES:
        for path in tiffs(base / "couleur" / feature):
            if not path.is_relative_to(base):
                raise ValueError("Image escapes base: " + str(path))
            before = native.signature(path)
            declaration, xmp = native.embedded_metadata(path)
            digest = native.sha256(path)
            native.verify_unchanged(path, before, digest)
            snapshots[str(path)] = (before, digest)
            item = {"file": str(path), "sha256": digest, "folder_feature": feature,
                    "embedded_ipr": declaration, "xmp_settings": xmp, "issues": []}
            entries.append(item)
            if declaration:
                if declaration.get("feature") != feature:
                    item["issues"].append("embedded_feature_does_not_match_folder")
                    continue
                relative = declaration.get("reference")
                if not relative or Path(relative).is_absolute() or ".." in Path(relative).parts:
                    raise ValueError("Invalid embedded IPR reference")
                reference = native.local_path(relative, base)
                if not reference.is_relative_to(base):
                    raise ValueError("Reference escapes base")
                ref, digest0 = {}, None
                if reference.is_file():
                    before0 = native.signature(reference)
                    ref, _ = native.embedded_metadata(reference)
                    digest0 = native.sha256(reference)
                    native.verify_unchanged(reference, before0, digest0)
                    snapshots[str(reference)] = (before0, digest0)
                    if not ref or ref.get("scene") != declaration.get("scene"):
                        raise ValueError("Mismatched declared IPR neutral")
                else:
                    item["issues"].append("missing_IPR_declared_neutral")
                context = ("source_sha256", "executable_sha256", "dcp_profile", "lcp_profile", "lcp_sha256", "raw_normalization")
                if ref and any(ref.get(k) != declaration.get(k) for k in context):
                    item["issues"].append("IPR_development_context_changed")
                record = {"scene": declaration["scene"], "engine": "IPR", "feature": feature,
                          "value": declaration["value"], "file": str(path), "reference": str(reference),
                          "settings": declaration.get("settings"), "reference_settings": ref.get("settings"),
                          "expected_sha256": digest, "reference_sha256": digest0,
                          "provenance": {"kind": "embedded_tiff", "declaration": declaration},
                          "association_issues": list(item["issues"])}
                records.append(record)
                item.update(scene=record["scene"], engine="IPR", value=record["value"])
            elif "-IPR" in path.stem:
                item["issues"].append("IPR_missing_embedded_settings_not_inferred_from_name")
            else:
                source = xmp.get("RawFileName")
                value = audit.finite_number(xmp.get(audit.FIELDS[feature]))
                if not source or value is None:
                    item["issues"].append("missing_LR_source_or_control_XMP")
                    continue
                scene = Path(source.replace("\\", "/")).stem
                item.update(scene=scene, engine="LR", value=value)
                other = "Saturation" if feature == "vibrance" else "Vibrance"
                if audit.finite_number(xmp.get(other)) != 0:
                    item["issues"].append("secondary_global_colour_control_nonzero:" + other)
                pending.append(item)

    for item in pending:
        # Series zero is an explicitly audited support, not an invented As Shot.
        zeros = [candidate for candidate in pending if candidate["scene"] == item["scene"]
                 and candidate["folder_feature"] == item["folder_feature"]
                 and candidate["value"] == 0 and not candidate["issues"]]
        record = {"scene": item["scene"], "engine": "LR", "feature": item["folder_feature"],
                  "value": item["value"], "file": item["file"], "expected_sha256": item["sha256"],
                  "provenance": {"kind": "embedded_lr_xmp", "source": item["xmp_settings"]["RawFileName"],
                                 "reference_selection": "unique clean control-zero TIFF of same feature and scene"},
                  "association_issues": list(item["issues"])}
        if len(zeros) == 1:
            record.update(reference=zeros[0]["file"], reference_sha256=zeros[0]["sha256"])
        else:
            record["association_issues"].append("missing_or_ambiguous_clean_LR_zero")
        records.append(record)
    by_state = defaultdict(list)
    for record in records:
        # Wrong-control exports are not allowed to poison the genuine zero.
        if not record["association_issues"]:
            by_state[record["scene"], record["engine"], record["feature"], record["value"]].append(record)
    for matches in by_state.values():
        if len(matches) > 1:
            for record in matches:
                record["association_issues"].append("ambiguous_duplicate_state")
    return records, entries, snapshots


def comparisons(records):
    by_state = defaultdict(list)
    for record in records:
        by_state[record["scene"], record["feature"], record["value"], record["engine"]].append(record)
    pairs = []
    for scene, feature, value in sorted({key[:3] for key in by_state}):
        engines = {engine: [r for r in by_state[scene, feature, value, engine] if r["protocol_usable"]]
                   for engine in ("IPR", "LR")}
        if any(len(rows) != 1 for rows in engines.values()):
            continue
        ipr, lr = engines["IPR"][0], engines["LR"][0]
        declaration = ipr.get("provenance", {}).get("declaration", {})
        settings = lr["metadata"]["xmp_settings"]
        context_issues = []
        label = str(declaration.get("dcp_profile", "")).lower().replace(" ", "")
        if "cameraneutral" not in label or settings.get("CameraProfile") != "Camera Neutral":
            context_issues.append("Camera_Neutral_not_confirmed_in_both_engines")
        if declaration.get("raw_normalization") is not False:
            context_issues.append("IPR_normalization_not_confirmed_off")
        if ((declaration.get("lens_geometry_applied") or declaration.get("lens_vignetting_applied"))
                and audit.finite_number(settings.get("LensProfileEnable")) != 1):
            context_issues.append("different_optical_development")
        if declaration.get("chromatic_aberration_applied") and audit.finite_number(settings.get("AutoLateralCA")) != 1:
            context_issues.append("different_CA_development")
        chroma = {}
        for zone in ("low_chroma", "high_chroma"):
            a, b = (r["metrics"]["chroma_groups"][zone] for r in (ipr, lr))
            da, db = a["mean_chroma_delta"], b["mean_chroma_delta"]
            chroma[zone] = {"IPR": da, "LR": db, "IPR_minus_LR": da - db if da is not None and db is not None else None,
                            "IPR_samples": a["samples"], "LR_samples": b["samples"],
                            "IPR_relative_delta": a["mean_relative_chroma_delta"],
                            "LR_relative_delta": b["mean_relative_chroma_delta"]}
        pairs.append({"scene": scene, "feature": feature, "value": value, "chroma": chroma,
                      "IPR_file": ipr["file"], "LR_file": lr["file"],
                      "IPR_Y_ratio": ipr["metrics"]["Y_ratio"], "LR_Y_ratio": lr["metrics"]["Y_ratio"],
                      "IPR_clipped_percent": ipr["metrics"]["clipped_percent_variant"],
                      "LR_clipped_percent": lr["metrics"]["clipped_percent_variant"],
                      "context_issues": context_issues, "cross_engine_pixel_alignment": False,
                      "masks_are_own_engine_and_state_dependent": True})
    aggregates = []
    for feature, value in sorted({(p["feature"], p["value"]) for p in pairs}):
        rows = [p for p in pairs if p["feature"] == feature and p["value"] == value]
        for scope, selected in (("audited_internal_responses", rows),
                                ("compatible_declared_development", [p for p in rows if not p["context_issues"]])):
            item = {"feature": feature, "value": value, "scope": scope, "scenes": len(selected), "chroma": {}}
            for zone in ("low_chroma", "high_chroma"):
                item["chroma"][zone] = {}
                for field in ("IPR", "LR", "IPR_minus_LR"):
                    values = [p["chroma"][zone][field] for p in selected if p["chroma"][zone][field] is not None]
                    item["chroma"][zone][field + "_median"] = float(np.median(values)) if values else None
                    item["chroma"][zone][field + "_scenes"] = len(values)
            aggregates.append(item)
    return pairs, aggregates


def run(base, step=8, data_version=1, supersedes=None):
    records, inventory, snapshots = associations(base)
    if not records:
        raise ValueError("No vibrance/saturation TIFF states")
    print("Audited associations:", len(records), flush=True)
    result = campaign.analyse("couleur", records, step=step, data_version=data_version,
                              supersedes=supersedes)
    for record in result["records"]:
        issues = record.pop("association_issues", [])
        record["issues"].extend(issues)
        if issues and record["status"] != "excluded":
            record["status"] = "diagnostic"
            record["protocol_usable"] = False
    result["coverage"] = {s: sum(r["status"] == s for r in result["records"]) for s in ("measured", "diagnostic", "excluded")}
    result["progressions"] = campaign.progressions("couleur", result["records"])
    pairs, aggregates = comparisons(result["records"])
    expected = []
    scenes = sorted({r["scene"] for r in records})
    for scene in scenes:
        for engine in ("IPR", "LR"):
            for feature in FEATURES:
                usable = {r["value"] for r in result["records"] if r["scene"] == scene and r["engine"] == engine
                          and r["feature"] == feature and r["protocol_usable"]}
                expected.append({"scene": scene, "engine": engine, "feature": feature,
                                 "missing_or_ambiguous_values": [v for v in LEVELS if v not in usable]})
    result["comparison"] = {"scope": "vibrance/saturation only", "inventory": inventory,
                            "expected_levels": list(LEVELS), "state_coverage": expected,
                            "pairs": pairs, "aggregates": aggregates,
                            "LR_reference": "per-series explicitly audited zero; shared As Shot not substituted",
                            "source_sha256": native.sha256(Path(__file__)),
                            "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()}
    for path, (before, digest) in snapshots.items():
        native.verify_unchanged(path, before, digest)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, default=Path("tests/image/base"))
    parser.add_argument("--output-dir", type=Path, default=Path("docs/validation/raw"))
    parser.add_argument("--data-version", type=int, default=1)
    parser.add_argument("--supersedes", help="Previous immutable campaign identifier")
    parser.add_argument("--step", type=int, default=8)
    args = parser.parse_args(argv)
    try:
        result = run(args.base, args.step, args.data_version, args.supersedes)
        path = args.output_dir / (result["campaign_id"] + ".json")
        native.write_immutable(path, result)
        print(path, result["coverage"], "pairs:", len(result["comparison"]["pairs"]))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print("Comparison failed:", error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
