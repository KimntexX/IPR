"""Study spectral/directional noise descriptors; experimental, never a denoiser."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
from datetime import datetime, timezone

import numpy as np

from study_noise_neighbourhoods import ROOT, audited_pairs, git, runtime_env, sha256, sources

FEATURE_GROUPS = {
    "scale_only": ("log_fine_hh", "log_mid_hh"),
    "spectral_only": ("spectral_flatness", "top8_power_fraction"),
    "direction_only": ("log_lh_hh", "log_hl_hh", "hh_corr_x", "hh_corr_y"),
}
FEATURE_GROUPS["combined"] = sum(FEATURE_GROUPS.values(), ())


def features(tile, names):
    signature = tile["signature"]
    if not signature["valid"]:
        return None
    result = np.array([signature["features"][name] for name in names], dtype=float)
    if not np.isfinite(result).all():
        raise ValueError("Non-finite spectral descriptor")
    return result


def select_pairs(audit, rebaseline, root):
    """Two distinct validated pairs per selected ISO; most populated bins first."""
    available = {(a.name, b.name): (folder, a, b) for folder, a, b in audited_pairs(audit, root)}
    record = json.loads(Path(rebaseline).read_text(encoding="utf-8"))
    metadata = {Path(f["path"].replace("\\", "/")).name: f["metadata"]
                for g in json.loads(Path(audit).read_text(encoding="utf-8"))["groups"] for f in g["frames"]}
    selected = []
    hashes = set()
    for iso in (100, 1600, 6400, 51200):
        candidates = [p for p in record["pairs"] if p["bins"] and abs(p["mean_y_drift_a_vs_b"]) <= .02
                      and (p["a"], p["b"]) in available and metadata[p["a"]]["iso"] == iso]
        candidates.sort(key=lambda p: (-len(p["bins"]), p["a"]))
        for split in ("train", "heldout"):
            candidate = next((p for p in candidates if p["a_sha256"] not in hashes and p["b_sha256"] not in hashes), None)
            if candidate is None:
                raise ValueError(f"Two independent file pairs unavailable at ISO {iso}")
            hashes.update((candidate["a_sha256"], candidate["b_sha256"]))
            _, a, b = available[candidate["a"], candidate["b"]]
            # Never reuse a baseline if the physical inputs changed.
            if sha256(a) != candidate["a_sha256"] or sha256(b) != candidate["b_sha256"]:
                raise ValueError("Pair source hash changed")
            selected.append({"a": a, "b": b, "split": split, "metadata_a": metadata[a.name],
                             "metadata_b": metadata[b.name]})
    return selected


def fit_models(pairs):
    """Robust diagnostic envelopes. No tuning using held-out pairs or photos."""
    models = []
    for group, names in FEATURE_GROUPS.items():
        buckets = {}
        for pair in pairs:
            if pair["split"] != "train":
                continue
            for tile in pair["tiles"]:
                if tile["kind"] != "pair-difference":
                    continue
                v = features(tile, names)
                if v is not None:
                    buckets.setdefault((tile["plane"], tile["bin"]), []).append((pair["a_sha256"], v))
        for (plane, bin_id), rows in sorted(buckets.items()):
            pair_ids = sorted({r[0] for r in rows})
            if len(rows) < 64 or len(pair_ids) < 2:
                continue
            x = np.array([r[1] for r in rows])
            center = np.median(x, axis=0)
            scale = np.maximum(1.4826 * np.median(np.abs(x - center), axis=0), .02)
            scores = np.max(np.abs((x - center) / scale), axis=1)
            models.append({"group": group, "plane": plane, "bin": bin_id,
                           "feature_names": list(names), "center": center.tolist(), "scale": scale.tolist(),
                           "threshold": float(np.quantile(scores, .95)), "train_tiles": len(rows),
                           "train_pair_ids": pair_ids})
    return models


def classify(tile, model):
    v = features(tile, model["feature_names"])
    if v is None:
        return {"available": False, "reason": "degenerate signature"}
    score = float(np.max(np.abs((v - model["center"]) / model["scale"])))
    return {"available": True, "score": score, "within_envelope": score <= model["threshold"]}


def evaluate(pairs, scenes, controls, models):
    lookup = {(m["group"], m["plane"], m["bin"]): m for m in models}
    for owner in pairs + scenes:
        for tile in owner["tiles"]:
            tile["diagnostic"] = {group: classify(tile, model) if (model := lookup.get((group, tile["plane"], tile["bin"])))
                                  else {"available": False, "reason": "insufficient training support in this bin/plane"}
                                  for group in FEATURE_GROUPS}
    # Compare each scalar synthetic control with every supported Y bin. These are
    # repeated diagnostics of the same control, NOT independent observations.
    for control in controls:
        control["diagnostic_y_by_bin"] = [dict(group=m["group"], bin=m["bin"], **classify(control, m))
                                          for m in models if m["plane"] == 0]


def run_injection(args, stamp):
    parent = json.loads(args.injection_from.read_text(encoding="utf-8"))
    heldout = [p for p in parent["pairs"] if p["split"] == "heldout"]
    train_hashes = {p[k] for p in parent["pairs"] if p["split"] == "train" for k in ("a_sha256","b_sha256")}
    if any(p[k] in train_hashes for p in heldout for k in ("a_sha256","b_sha256")):
        raise ValueError("Train/heldout source leakage")
    paths = {Path(p).name: Path(p) for p in parent["source_paths"]}
    inputs = [paths[p[k]] for p in heldout for k in ("a","b")]
    for pair in heldout:
        for key in ("a","b"):
            if sha256(paths[pair[key]]) != pair[key+"_sha256"]:
                raise ValueError("Injection source hash changed")
    signatures = {p:(p.stat().st_size,p.stat().st_mtime_ns) for p in inputs}
    binary_hash = sha256(args.probe)
    output = args.output_dir / f"etude-injection-spectrale-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}.json"
    if output.exists():
        raise ValueError("Existing injection campaign")
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory)/"injections.json"
        result = subprocess.run([str(args.probe),"--spectral-injection",str(temporary),*map(str,inputs)],
                                env=runtime_env(args.probe),capture_output=True,text=True,timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(result.stderr[-3000:])
        record = json.loads(temporary.read_text(encoding="utf-8"))
    models = parent["models"]
    lookup = {(m["group"],m["plane"],m["bin"]):m for m in models}
    for pair, old in zip(record["pairs"],heldout,strict=True):
        if any(pair[k]!=old[k] for k in ("a","b","a_sha256","b_sha256")):
            raise ValueError("Unexpected injection pair identity")
        pair.update(split="heldout",metadata_a=old["metadata_a"],metadata_b=old["metadata_b"])
        for item in pair["injections"]:
            item["diagnostic"] = {group:classify(item,model) if (model:=lookup.get((group,item["plane"],item["bin"])))
                                  else {"available":False,"reason":"insufficient frozen model support"}
                                  for group in FEATURE_GROUPS}
        del pair["tiles"]  # original D/A signatures live in the referenced parent campaign
    for p, before in signatures.items():
        if (p.stat().st_size,p.stat().st_mtime_ns)!=before:
            raise ValueError("Injection input changed")
    if sha256(args.probe)!=binary_hash:
        raise ValueError("Injection binary changed")
    record.pop("controls")
    record.update(schema_version=1,data_version=args.data_version,measured_at_utc=stamp.isoformat(),campaign_id=output.stem,
                  parent_campaign=dict(path=str(args.injection_from),sha256=sha256(args.injection_from)),
                  models=models,decision_rule=parent["decision_rule"],source_paths=[str(p) for p in inputs],
                  commit=git("rev-parse","HEAD"),local_changes=git("status","--short").splitlines(),
                  runner_sha256=sha256(Path(__file__)),probe_sha256=binary_hash,
                  protocol="docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md",
                  injection_rule="two sampled D tiles per bin/plane (first and middle); known clean shapes peak=1/3/10 sample noise SD, flat only once, no noise rescaling; fixed parent envelopes, heldout pairs only",
                  limitations="post-development differential noise injection, not physical RAW or single-frame marginal noise; reused noise across motifs, correlated tests; no filtering/quality claim",
                  duration_seconds=round(time.monotonic()-start,1))
    with output.open("x",encoding="utf-8") as handle:
        json.dump(record,handle,ensure_ascii=False,indent=1,allow_nan=False)
    print(output)
    print(f"{sum(len(p['injections']) for p in record['pairs'])} injections, fixed models, {record['duration_seconds']} s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--audit", type=Path)
    parser.add_argument("--rebaseline", type=Path)
    parser.add_argument("--pairs-root", type=Path)
    parser.add_argument("--base", type=Path, action="append")
    parser.add_argument("--scene", action="append")
    parser.add_argument("--injection-from", type=Path, help="supplement a spectral campaign using its frozen models")
    parser.add_argument("--probe", type=Path, default=ROOT / "build-patch_noise/Release/noiseNeighbourhoodProbe.exe")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    stamp = datetime.now(timezone.utc)
    if args.injection_from:
        return run_injection(args,stamp)
    if not all((args.audit,args.rebaseline,args.pairs_root,args.base,args.scene)):
        parser.error("Initial study needs --audit, --rebaseline, --pairs-root, --base and --scene")
    output = args.output_dir / f"etude-spectrale-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}.json"
    if output.exists():
        raise SystemExit("Existing campaign, nothing overwritten")
    selected = select_pairs(args.audit, args.rebaseline, args.pairs_root)
    raws = sources(args.base, args.scene)
    inputs = raws + [p[k] for p in selected for k in ("a", "b")]
    signatures = {p: (p.stat().st_size, p.stat().st_mtime_ns) for p in inputs}
    binary_hash = sha256(args.probe)
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as directory:
        records = []
        for kind, paths in (("pairs", [p[k] for p in selected for k in ("a", "b")]), ("scenes", raws)):
            temporary = Path(directory) / (kind + ".json")
            result = subprocess.run([str(args.probe), "--spectral-" + kind, str(temporary), *map(str, paths)],
                                    env=runtime_env(args.probe), capture_output=True, text=True, timeout=args.timeout)
            if result.returncode:
                raise RuntimeError(result.stderr[-3000:])
            records.append(json.loads(temporary.read_text(encoding="utf-8")))
    pair_data, scene_data = records
    for pair, spec in zip(pair_data["pairs"], selected, strict=True):
        if pair["a"] != spec["a"].name or pair["b"] != spec["b"].name:
            raise ValueError("Unexpected native pair order")
        pair.update(split=spec["split"], metadata_a=spec["metadata_a"], metadata_b=spec["metadata_b"])
    models = fit_models(pair_data["pairs"])
    controls = scene_data["controls"]
    if controls != pair_data["controls"]:
        raise ValueError("Synthetic controls changed between runs")
    evaluate(pair_data["pairs"], scene_data["scenes"], controls, models)
    for p, before in signatures.items():
        if (p.stat().st_size, p.stat().st_mtime_ns) != before:
            raise ValueError("Source changed during study: " + str(p))
    if binary_hash != sha256(args.probe):
        raise ValueError("Probe binary changed during study")
    record = dict(schema_version=1, data_version=args.data_version, measured_at_utc=stamp.isoformat(),
                  campaign_id=output.stem, commit=git("rev-parse", "HEAD"), local_changes=git("status", "--short").splitlines(),
                  protocol="docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md",
                  runner_sha256=sha256(Path(__file__)), probe_sha256=binary_hash,
                  source_paths=[str(p) for p in inputs],
                  source_documents=[dict(path=str(p), sha256=sha256(p)) for p in (args.audit, args.rebaseline)],
                  feature_definition=pair_data["feature_definition"], units=pair_data["units"],
                  decision_rule="diagnostic only, per plane/Y bin pooled ISO: >=64 D tiles from >=2 training pairs; median/1.4826 MAD with scale floor .02; max absolute standardized feature, train q95 threshold; no probability; unmatched bins abstain",
                  limitations="R6 III pairs only; few paired scenes, tiles correlated, D not marginal single-frame truth after nonlinear processing, synthetic noise not RCD; compatibility with noise is not proof of noise; no denoising or timing validation",
                  duration_seconds=round(time.monotonic()-start, 1), pairs=pair_data["pairs"],
                  scenes=scene_data["scenes"], controls=controls, models=models)
    with output.open("x", encoding="utf-8") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=1, allow_nan=False)
    print(output)
    print(f"{len(selected)} pairs, {len(raws)} scenes, {len(models)} envelopes, {record['duration_seconds']} s")


if __name__ == "__main__":
    main()
