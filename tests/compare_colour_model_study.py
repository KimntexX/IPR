"""Compare diagnostic candidates, without fitting them to Lightroom or touching images."""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path
import json
import numpy as np
from analysis import native


def summarize(probe, reference):
    if reference.get("group") != "couleur" or reference.get("coverage") != {"measured": 196, "diagnostic": 0, "excluded": 0}:
        raise ValueError("Expected complete audited v003 colour reference")
    refs = {(r["scene"], r["engine"], r["feature"], r["value"]): r for r in reference["records"]}
    sources = {s["scene"]: s for s in probe["sources"]}
    values = (-100, -50, -25, 0, 25, 50, 100)
    features = ("vibrance", "saturation")
    models = ("current", "A1_endpoint_rgb", "A2_adaptive_oklch", "B_pr598_prophoto")
    expected_refs = {(scene, engine, feature, value) for scene in sources
                     for engine in ("IPR", "LR") for feature in features for value in values}
    observed_probe = [(r["scene"], r["model"], r["feature"], r["value"]) for r in probe["records"]]
    expected_probe = {(scene, model, feature, value) for scene in sources
                      for model in models for feature in features for value in values}
    if (len(sources) != 7 or len(probe["sources"]) != 7 or len(reference["records"]) != 196
            or set(refs) != expected_refs or len(observed_probe) != 392
            or set(observed_probe) != expected_probe):
        raise ValueError("Incomplete or duplicated study records")
    for scene, source in sources.items():
        declaration = refs[scene, "IPR", "vibrance", 0]["provenance"]["declaration"]
        for a, b in (("sha256", "source_sha256"), ("dcp", "dcp_profile"), ("lcp_sha256", "lcp_sha256"),
                     ("lens_geometry", "lens_geometry_applied"), ("lens_vignetting", "lens_vignetting_applied"),
                     ("CA", "chromatic_aberration_applied"), ("normalization", "raw_normalization")):
            if source[a] != declaration[b]:
                raise ValueError("Incompatible study context: " + scene + " " + a)
    baseline_errors = []
    pairs = []
    for r in probe["records"]:
        key = (r["scene"], "IPR", r["feature"], r["value"])
        previous = refs[key]["metrics"]
        current = r["metrics"]
        if r["model"] == "current":
            differences = {k: abs(current[k] - previous[k]) for k in ("Y_ratio", "mean_Y_neutral", "mean_Y_variant")}
            for zone in ("low_chroma", "high_chroma"):
                for k in ("mean_chroma_delta", "mean_relative_chroma_delta"):
                    differences[zone + "." + k] = abs(current["chroma_groups"][zone][k] - previous["chroma_groups"][zone][k])
            baseline_errors.append({"scene": r["scene"], "feature": r["feature"], "value": r["value"], "differences": differences})
        lr = refs[r["scene"], "LR", r["feature"], r["value"]]
        zones = {}
        for zone in ("low_chroma", "high_chroma"):
            a, b = current["chroma_groups"][zone], lr["metrics"]["chroma_groups"][zone]
            zones[zone] = {"candidate_delta": a["mean_chroma_delta"], "LR_delta": b["mean_chroma_delta"],
                           "candidate_relative_delta": a["mean_relative_chroma_delta"], "LR_relative_delta": b["mean_relative_chroma_delta"],
                           "response_difference": a["mean_chroma_delta"] - b["mean_chroma_delta"],
                           "candidate_samples": a["samples"], "LR_samples": b["samples"]}
        pairs.append({"scene": r["scene"], "model": r["model"], "feature": r["feature"], "value": r["value"],
                      "zones": zones, "candidate_Y_ratio": current["Y_ratio"], "LR_Y_ratio": lr["metrics"]["Y_ratio"],
                      "candidate_pre_output_Y_ratio": current["pre_output_Y_ratio"],
                      "candidate_outside_srgb_percent": current["pre_output_outside_srgb_percent"],
                      "candidate_newly_clipped_samples": current["newly_clipped_samples"],
                      "different_optical_development": True, "pixel_alignment": False})
    maximum = max(v for row in baseline_errors for v in row["differences"].values())
    if maximum > 2e-6:
        raise ValueError(f"Current probe/reference mismatch {maximum}; no candidate ranking permitted")
    grouped = defaultdict(list)
    for p in pairs:
        grouped[p["model"], p["feature"], p["value"]].append(p)
    aggregates = []
    for (model, feature, value), rows in sorted(grouped.items()):
        a = {"model": model, "feature": feature, "value": value, "scenes": len(rows), "zones": {}}
        for zone in ("low_chroma", "high_chroma"):
            a["zones"][zone] = {k + "_median": float(np.median([r["zones"][zone][k] for r in rows]))
                                 for k in ("candidate_delta", "LR_delta", "candidate_relative_delta", "LR_relative_delta", "response_difference")}
            a["zones"][zone]["absolute_response_difference_median"] = float(np.median([abs(r["zones"][zone]["response_difference"]) for r in rows]))
        for k in ("candidate_Y_ratio", "LR_Y_ratio", "candidate_pre_output_Y_ratio", "candidate_outside_srgb_percent"):
            a[k + "_median"] = float(np.median([r[k] for r in rows]))
            a[k + "_min"] = min(r[k] for r in rows)
            a[k + "_max"] = max(r[k] for r in rows)
        a["candidate_newly_clipped_samples_sum"] = sum(r["candidate_newly_clipped_samples"] for r in rows)
        aggregates.append(a)
    progressions = []
    indexed = defaultdict(list)
    for p in pairs:
        indexed[p["scene"], p["model"], p["feature"]].append(p)
    for key, rows in sorted(indexed.items()):
        ordered = sorted(rows, key=lambda p: p["value"])
        points = [p["zones"]["low_chroma"]["candidate_delta"] for p in ordered]
        progressions.append({"scene": key[0], "model": key[1], "feature": key[2],
                             "values": [p["value"] for p in ordered], "mean_low_chroma_deltas": points,
                             "monotonic_observed_means": all(b + 1e-4 >= a for a, b in zip(points, points[1:])),
                             "tolerance": 1e-4, "masks_vary_by_state": True})
    return {"baseline_max_absolute_metric_difference": maximum, "baseline_checks": baseline_errors,
            "pairs": pairs, "aggregates": aggregates, "progressions": progressions,
            "decision": {"production_modified": False, "visual": "not_evaluated", "fit_to_LR": False,
                         "recommended_model": None, "cleanup_eligible": False},
            "limitations": ["Stored LR responses from the reference JSON, not newly inspected LR pixels",
                            "Comparison of own responses on independent engine masks, no inter-engine MAE/DeltaE",
                            "LR closeness is descriptive, not the optimization objective or a quality score",
                            "No parameter search, no training/validation split: predefined exploratory candidates"]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--output-dir", type=Path, default=Path("docs/validation/raw"))
    args = parser.parse_args(argv)
    try:
        inputs = [(p, native.signature(p), native.sha256(p)) for p in (args.probe, args.reference)]
        probe, reference = [json.loads(p.read_text(encoding="utf-8")) for p, _, _ in inputs]
        result = summarize(probe, reference)
        now = datetime.now(timezone.utc)
        result.update(campaign_id="comparaison-modeles-couleur-v001-" + now.strftime("%Y%m%dT%H%M%S%fZ"),
                      schema_version=1, data_version=1, measured_at_utc=now.isoformat(), group="couleur",
                      probe_campaign_id=probe["campaign_id"], reference_campaign_id=reference["campaign_id"],
                      input_files=[{"file": str(p), "sha256": digest} for p, _, digest in inputs],
                      analyser_sha256=native.sha256(Path(__file__)))
        for p, before, digest in inputs:
            native.verify_unchanged(p, before, digest)
        target = args.output_dir / (result["campaign_id"] + ".json")
        native.write_immutable(target, result)
        print(target, "pairs:", len(result["pairs"]), "baseline difference:", result["baseline_max_absolute_metric_difference"])
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print("Study comparison failed:", error)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
