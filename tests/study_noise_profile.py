"""Noise profile of E from spectrally compatible tiles, checked against held-out pair residuals.

E estimates its noise profile blind, on the image alone: per Y bin and plane,
the 25th percentile of the Haar HH sigma of all 32x32 tiles, divided by a bias
measured on pairs, empty bins copied from the nearest valid bin. Texture in a
bin inflates it; a copied bin describes another brightness.

Profile S keeps only the tiles of frame A whose spectral signature is within the
combined envelope of the parent spectral campaign (frozen, fitted on its
training pairs only), takes their median HH sigma, and abstains where a bin has
fewer than MIN_TILES compatible tiles or no envelope: no copy from another bin.
Its one scale factor per plane is calibrated on the training pairs only.

The parent envelopes cover only bins 9 to 12, yet their centres differ by less
than 0.05 between those bins, well within their scales: the dimensionless
signature of developed noise barely depends on brightness. Profile P therefore
uses one envelope per plane, pooled over all training D tiles of every bin
(median and 1.4826 MAD, floor 0.02, threshold the 95th percentile of the
training scores, as the parent rule), with the other rules of S.

Both are compared, on the held-out pairs, with the truth: the median sigma of
D = (A - B)/sqrt(2) over quiet tiles of the bin (mean-image gradient below 3
HH sigmas of D(Y)), per plane (Y, Co, Cg), bin and scale (Haar HH; G1 - G4 band,
predicted from HH by E's spectral ratios, and measured directly on the
compatible tiles for S). Score: log2(estimate / truth). Writes one immutable,
versioned JSON.

Usage: python tests/study_noise_profile.py --data-version N --parent SPECTRAL.json
"""
import argparse
import json
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from study_noise_neighbourhoods import ROOT, git, runtime_env, sha256
from study_noise_spectral import FEATURE_GROUPS

PLANES = ("y", "co", "cg")
MIN_TILES = 16           # compatible tiles needed to estimate a bin
MIN_TRUTH_TILES = 8      # quiet D tiles needed for the truth of a bin
E_MIN_TILES = 8          # E's own rule
# E's constants (tests/noise_neighbourhood_probe.cpp, synthese-bruit-rcd-v001): bias of
# q25 per plane, and band ratios to Haar HH (mid = G1 - G4, index 2).
E_BIAS = (0.933, 0.874, 0.861)
E_MID_RATIO = (0.614, 2.095, 2.740)


def table(record):
    """Rows of a probe pair as a dict of column arrays."""
    columns = record["columns"]
    rows = np.array(record["rows"], dtype=float)
    return {name: rows[:, i] for i, name in enumerate(columns)}


def pooled_models(parent):
    """One combined envelope per plane from all training D tiles of the parent campaign, every bin."""
    names = FEATURE_GROUPS["combined"]
    models = {}
    for plane in range(len(PLANES)):
        x = np.array([[t["signature"]["features"][n] for n in names] for p in parent["pairs"] if p["split"] == "train"
                      for t in p["tiles"] if t["kind"] == "pair-difference" and t["plane"] == plane
                      and t["signature"]["valid"]])
        center = np.median(x, axis=0)
        scale = np.maximum(1.4826 * np.median(np.abs(x - center), axis=0), .02)
        scores = np.max(np.abs((x - center) / scale), axis=1)
        models[plane] = {"center": center.tolist(), "scale": scale.tolist(),
                         "threshold": float(np.quantile(scores, .95)), "train_tiles": int(len(x))}
    return models


def compatible(t, plane, models, pooled=None):
    """Mask of tiles of frame A within the combined envelope: per bin (frozen parent) or pooled per plane."""
    names = FEATURE_GROUPS["combined"]
    x = np.stack([t[f"{plane}_{n}"] for n in names], axis=1)
    ok = np.zeros(len(x), dtype=bool)
    index = PLANES.index(plane)
    if pooled is not None:
        m = pooled[index]
        sel = t[f"{plane}_valid"] > 0
        score = np.max(np.abs((x[sel] - np.array(m["center"])) / np.array(m["scale"])), axis=1)
        ok[np.flatnonzero(sel)[score <= m["threshold"]]] = True
        return ok
    for (group, p, b), m in models.items():
        if group != "combined" or p != index:
            continue
        sel = (t["bin_a"] == b) & (t[f"{plane}_valid"] > 0)
        if not sel.any():
            continue
        score = np.max(np.abs((x[sel] - np.array(m["center"])) / np.array(m["scale"])), axis=1)
        ok[np.flatnonzero(sel)[score <= m["threshold"]]] = True
    return ok


def estimate(t, models, pooled):
    """Per plane and bin: truth, E, S and P (raw, before calibration), HH and mid band."""
    out = {}
    bins = sorted({int(b) for b in t["bin_a"]} | {int(b) for b in t["bin_m"] if b >= 0})
    for plane in PLANES:
        c = PLANES.index(plane)
        mask = compatible(t, plane, models)
        mask_p = compatible(t, plane, models, pooled)
        e_hh = {}
        for b in bins:
            v = t[f"{plane}_hh_a"][t["bin_a"] == b]
            if len(v) >= E_MIN_TILES:
                e_hh[b] = float(np.quantile(v, .25)) / E_BIAS[c]
        rows = {}
        for b in bins:
            quiet = (t["bin_m"] == b) & (t["quiet"] > 0)
            truth_hh = float(np.median(t[f"{plane}_hh_d"][quiet])) if quiet.sum() >= MIN_TRUTH_TILES else None
            truth_mid = float(np.median(t[f"{plane}_mid_d"][quiet])) if quiet.sum() >= MIN_TRUTH_TILES else None
            filled = b not in e_hh
            if filled and e_hh:
                nearest = min(e_hh, key=lambda k: (abs(k - b), k))
                e_value = e_hh[nearest]
            else:
                e_value = e_hh.get(b)
            sel = mask & (t["bin_a"] == b)
            n = int(sel.sum())
            sel_p = mask_p & (t["bin_a"] == b)
            n_p = int(sel_p.sum())
            rows[b] = {"truth_hh": truth_hh, "truth_mid": truth_mid, "truth_tiles": int(quiet.sum()),
                       "tiles": int((t["bin_a"] == b).sum()),
                       "e_hh": e_value, "e_mid": None if e_value is None else e_value * E_MID_RATIO[c],
                       "e_filled_from_other_bin": bool(filled and e_value is not None),
                       "compatible_tiles": n,
                       "s_hh_raw": float(np.median(t[f"{plane}_hh_a"][sel])) if n >= MIN_TILES else None,
                       "s_mid_raw": float(np.median(t[f"{plane}_mid_a"][sel])) if n >= MIN_TILES else None,
                       "pooled_compatible_tiles": n_p,
                       "p_hh_raw": float(np.median(t[f"{plane}_hh_a"][sel_p])) if n_p >= MIN_TILES else None,
                       "p_mid_raw": float(np.median(t[f"{plane}_mid_a"][sel_p])) if n_p >= MIN_TILES else None}
        out[plane] = rows
    return out


def log_ratio(value, truth):
    return None if value is None or truth is None or value <= 0 or truth <= 0 else float(np.log2(value / truth))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--parent", type=Path, required=True, help="spectral campaign with frozen envelopes")
    parser.add_argument("--probe", type=Path, default=ROOT / "build-patch_noise/Release/noiseNeighbourhoodProbe.exe")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--timeout", type=int, default=3600)
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    output = args.output_dir / f"etude-profil-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}.json"
    if output.exists():
        raise SystemExit("Existing campaign, nothing overwritten")
    parent = json.loads(args.parent.read_text(encoding="utf-8"))
    models = {(m["group"], m["plane"], m["bin"]): m for m in parent["models"]}
    pooled = pooled_models(parent)
    paths = {Path(p).name: Path(p) for p in parent["source_paths"]}
    pairs = [{k: p[k] for k in ("a", "b", "a_sha256", "b_sha256", "split")} | {"iso": p["metadata_a"]["iso"]}
             for p in parent["pairs"]]
    for p in pairs:
        for k in ("a", "b"):
            if sha256(paths[p[k]]) != p[k + "_sha256"]:
                raise ValueError("Pair source hash changed: " + p[k])
    probe_hash = sha256(args.probe)
    start = time.monotonic()
    with tempfile.TemporaryDirectory() as directory:
        temporary = Path(directory) / "profile.json"
        command = [str(args.probe), "--profile-pairs", str(temporary)]
        for p in pairs:
            command += [str(paths[p["a"]]), str(paths[p["b"]])]
        result = subprocess.run(command, env=runtime_env(args.probe), capture_output=True, text=True,
                                timeout=args.timeout)
        if result.returncode:
            raise RuntimeError(result.stderr[-3000:])
        native = json.loads(temporary.read_text(encoding="utf-8"))
    if sha256(args.probe) != probe_hash:
        raise ValueError("Probe binary changed during study")
    for p, record in zip(pairs, native["pairs"], strict=True):
        if record["a"] != p["a"] or record["b"] != p["b"]:
            raise ValueError("Unexpected native pair order")
        p["mean_y_drift"] = record["mean_y_drift"]
        p["bins"] = estimate(table(record), models, pooled)
    # S and P calibration: one factor per plane and scale, median truth / raw over training cells.
    calibration = {}
    for profile in ("s", "p"):
        for plane in PLANES:
            for scale in ("hh", "mid"):
                ratios = [r[f"truth_{scale}"] / r[f"{profile}_{scale}_raw"] for p in pairs if p["split"] == "train"
                          for r in p["bins"][plane].values()
                          if r[f"truth_{scale}"] and r[f"{profile}_{scale}_raw"]]
                calibration[f"{profile}_{plane}_{scale}"] = float(np.median(ratios)) if ratios else None
    summary = {}
    for p in pairs:
        for plane in PLANES:
            for b, r in p["bins"][plane].items():
                for scale in ("hh", "mid"):
                    for profile in ("s", "p"):
                        k = calibration[f"{profile}_{plane}_{scale}"]
                        raw = r[f"{profile}_{scale}_raw"]
                        r[f"{profile}_{scale}"] = None if raw is None or k is None else raw * k
                        r[f"{profile}_{scale}_log2_error"] = log_ratio(r[f"{profile}_{scale}"], r[f"truth_{scale}"])
                    r[f"e_{scale}_log2_error"] = log_ratio(r[f"e_{scale}"], r[f"truth_{scale}"])
                if p["split"] != "heldout":
                    continue
                for scale in ("hh", "mid"):
                    key = (plane, scale)
                    s = summary.setdefault(key, {"truth_cells": 0, "e": [], "e_measured_bin": [], "s": [],
                                                 "e_on_s_cells": [], "s_abstained": 0, "p": [], "e_on_p_cells": [],
                                                 "p_abstained": 0, "by_iso": {}})
                    if r[f"truth_{scale}"] is None:
                        continue
                    s["truth_cells"] += 1
                    iso = s["by_iso"].setdefault(str(p["iso"]), {"e": [], "s": [], "s_abstained": 0, "p": [],
                                                                 "p_abstained": 0})
                    if r[f"p_{scale}_log2_error"] is None:
                        s["p_abstained"] += 1
                        iso["p_abstained"] += 1
                    else:
                        s["p"].append(r[f"p_{scale}_log2_error"])
                        iso["p"].append(r[f"p_{scale}_log2_error"])
                        if r[f"e_{scale}_log2_error"] is not None:
                            s["e_on_p_cells"].append(r[f"e_{scale}_log2_error"])
                    if r[f"e_{scale}_log2_error"] is not None:
                        s["e"].append(r[f"e_{scale}_log2_error"])
                        iso["e"].append(r[f"e_{scale}_log2_error"])
                        if not r["e_filled_from_other_bin"]:
                            s["e_measured_bin"].append(r[f"e_{scale}_log2_error"])
                    if r[f"s_{scale}_log2_error"] is None:
                        s["s_abstained"] += 1
                        iso["s_abstained"] += 1
                    else:
                        s["s"].append(r[f"s_{scale}_log2_error"])
                        iso["s"].append(r[f"s_{scale}_log2_error"])
                        if r[f"e_{scale}_log2_error"] is not None:
                            s["e_on_s_cells"].append(r[f"e_{scale}_log2_error"])

    def stats(v):
        v = np.abs(np.array(v))
        return {"cells": int(v.size), "median_abs_log2": float(np.median(v)) if v.size else None,
                "max_abs_log2": float(v.max()) if v.size else None}

    report = {}
    for (plane, scale), s in summary.items():
        report[f"{plane}_{scale}"] = {
            "truth_cells": s["truth_cells"], "s_abstained": s["s_abstained"],
            "e_all": stats(s["e"]), "e_measured_bins": stats(s["e_measured_bin"]),
            "e_on_s_cells": stats(s["e_on_s_cells"]), "s": stats(s["s"]),
            "p_abstained": s["p_abstained"], "e_on_p_cells": stats(s["e_on_p_cells"]), "p": stats(s["p"]),
            "by_iso": {iso: {"e": stats(v["e"]), "s": stats(v["s"]), "s_abstained": v["s_abstained"],
                             "p": stats(v["p"]), "p_abstained": v["p_abstained"]}
                       for iso, v in sorted(s["by_iso"].items(), key=lambda kv: int(kv[0]))}}
        r = report[f"{plane}_{scale}"]
        print(f"{plane:2s} {scale:3s}: truth cells {s['truth_cells']:2d} | E all {r['e_all']['median_abs_log2']:.3f} "
              f"(max {r['e_all']['max_abs_log2']:.2f}) | E on S cells {r['e_on_s_cells']['median_abs_log2'] or 0:.3f} | "
              f"S {r['s']['median_abs_log2'] or 0:.3f} (max {r['s']['max_abs_log2'] or 0:.2f}, {r['s']['cells']} cells, "
              f"{s['s_abstained']} abstained) | P {r['p']['median_abs_log2'] or 0:.3f} (max {r['p']['max_abs_log2'] or 0:.2f}, "
              f"{r['p']['cells']} cells; E on them {r['e_on_p_cells']['median_abs_log2'] or 0:.3f})", flush=True)
    record = {"schema_version": 1, "data_version": args.data_version, "measured_at_utc": stamp.isoformat(),
              "campaign_id": output.stem, "runner": "tests/study_noise_profile.py", "runner_sha256": sha256(Path(__file__)),
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "probe_sha256": probe_hash, "parent_campaign": {"path": str(args.parent), "sha256": sha256(args.parent)},
              "protocol": "docs/validation/protocols/protocole-bruit-voisinages-adaptatifs.md",
              "units": native["units"],
              "rules": {"e": f"q25 of all tiles of the bin / bias {E_BIAS}, >= {E_MIN_TILES} tiles, else nearest bin",
                        "s": f"median of frame-A tiles within the frozen combined envelope, >= {MIN_TILES} tiles, "
                             "else abstain; one factor per plane and scale from training pairs",
                        "p": "as s, with one combined envelope per plane pooled over all training D tiles",
                        "truth": f"median of D over quiet tiles of the mean-image bin, >= {MIN_TRUTH_TILES} tiles",
                        "e_mid": f"E HH times its band ratio {E_MID_RATIO}",
                        "score": "log2(estimate / truth), held-out pairs only"},
              "calibration": calibration, "pooled_envelopes": pooled,
              "limitations": "R6 Mark III pairs only, four held-out pairs; D is a differential residual after "
                             "development, not the marginal noise of one frame; tiles are correlated; E's bias "
                             "constants come from earlier pair summaries that may include these pairs",
              "duration_seconds": round(time.monotonic() - start, 1),
              "pairs": [{**{k: v for k, v in p.items() if k != "bins"},
                         "bins": {plane: {str(b): r for b, r in rows.items()} for plane, rows in p["bins"].items()}}
                        for p in pairs],
              "summary": report}
    with output.open("x", encoding="utf-8") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=1, allow_nan=False)
    print(output)
    print(f"{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
