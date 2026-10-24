"""Scene adaptation of Lightroom's clarity: what IPR's band model leaves, per scene.

For each clarity state, Lightroom's change of log2 luminance (state against its
neutral) is compared with IPR's (rendered in memory, state against its
neutral), pixel to pixel after a 4x area reduction (both engines share the
RAW geometry up to a one-pixel shift, negligible at that scale). The residual
is described per scene by its median per neutral level, the share of its
variance that this level curve explains, and the share explained by a table of
(level, level minus its 64-pixel Gaussian mean). Statistics of the Lightroom
neutral are recorded as candidate predictors. Writes one immutable, versioned JSON.

Usage: python tests/study_clarity_adaptation.py --data-version N [--scene STEM ...]
"""
import argparse
import json
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis import inputs  # noqa: E402
from analysis.render import RenderServer, find_server  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

WEIGHTS = np.array([.2126, .7152, .0722], np.float32)
REDUCTION = 4
LEVELS = np.array([-12, -8, -6, -5, -4, -3, -2, -1, 0.5])   # log2 of the neutral luminance


def reduced_log(rgb):
    y = np.clip(rgb @ WEIGHTS, 0, None)
    y = cv2.resize(y, (y.shape[1] // REDUCTION, y.shape[0] // REDUCTION), interpolation=cv2.INTER_AREA)
    return np.log2(y + 1e-3).astype(np.float32)


def statistics(log_neutral):
    q = np.quantile(log_neutral, [0.02, 0.1, 0.25, 0.5, 0.75, 0.9, 0.98])
    large = cv2.GaussianBlur(log_neutral, (0, 0), 64 / REDUCTION)
    local = log_neutral - cv2.GaussianBlur(log_neutral, (0, 0), 4 / REDUCTION)
    return {"p02": float(q[0]), "p10": float(q[1]), "p25": float(q[2]), "p50": float(q[3]), "p75": float(q[4]),
            "p90": float(q[5]), "p98": float(q[6]), "range": float(q[6] - q[0]), "mean": float(log_neutral.mean()),
            "large_std": float(large.std()), "local_std": float(local.std()),
            "dark_share": float((log_neutral < -7).mean()), "bright_share": float((log_neutral > -1.5).mean())}


def describe(level, residual, relative):
    total = float(np.var(residual))
    bins = np.clip(np.searchsorted(LEVELS, level) - 1, 0, len(LEVELS) - 2)
    curve, explained = [], np.zeros_like(residual)
    for i in range(len(LEVELS) - 1):
        m = bins == i
        med = float(np.median(residual[m])) if m.sum() >= 500 else None
        curve.append({"level_log2": float((LEVELS[i] + LEVELS[i + 1]) / 2), "pixels": int(m.sum()), "median": med})
        if med is not None:
            explained[m] = med
    r2_level = 1 - float(np.var(residual - explained)) / total if total > 0 else 0.0
    rq = np.quantile(relative, np.linspace(0, 1, 17))
    rb = np.clip(np.searchsorted(rq, relative) - 1, 0, 15)
    key = bins * 16 + rb
    means = np.bincount(key, weights=residual, minlength=(len(LEVELS) - 1) * 16) / np.maximum(
        np.bincount(key, minlength=(len(LEVELS) - 1) * 16), 1)
    r2_local = 1 - float(np.var(residual - means[key])) / total if total > 0 else 0.0
    return {"curve": curve, "r2_level_curve": r2_level, "r2_level_relative_table": r2_local,
            "residual_std": float(np.sqrt(total))}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path)
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-clarte-adaptation-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    records = [r for r in inputs.base_records(args.base, ["effets"])
               if r["feature"] == "clarity" and r.get("reference") and (not args.scene or r["scene"] in args.scene)]
    raws = {p.stem: p for p in sources(args.base, [])}
    start = time.monotonic()
    scenes = {}
    server = RenderServer(args.server or find_server(), timeout=300)
    try:
        for r in sorted(records, key=lambda r: (r["scene"], r["value"])):
            raw = raws.get(r["scene"])
            if raw is None:
                continue
            entry = scenes.setdefault(r["scene"], {"raw": raw.name, "states": {}})
            lr0 = reduced_log(linear(read_tiff(r["reference"])))
            lr1 = reduced_log(linear(read_tiff(r["file"])))
            if "statistics" not in entry:
                entry["statistics"] = statistics(lr0)
                entry["_ipr0"] = reduced_log(linear(server.render(raw, "neutral", 0)[0]))
            ipr1 = reduced_log(linear(server.render(raw, "clarity", int(r["value"]))[0]))
            ipr0 = entry["_ipr0"]
            h, w = min(lr0.shape[0], ipr0.shape[0]), min(lr0.shape[1], ipr0.shape[1])
            if lr0.shape[:2] != ipr0.shape[:2] and (lr0.shape[1] > lr0.shape[0]) != (ipr0.shape[1] > ipr0.shape[0]):
                raise SystemExit("Orientation differs between engines: " + r["scene"])
            lr_change = (lr1 - lr0)[:h, :w]
            ipr_change = (ipr1 - ipr0)[:h, :w]
            level = lr0[:h, :w]
            relative = level - cv2.GaussianBlur(level, (0, 0), 64 / REDUCTION)
            valid = (level > -11) & (lr1[:h, :w] < 0)
            residual = (lr_change - ipr_change)[valid]
            state = describe(level[valid], residual, relative[valid])
            state["lr_change_std"] = float(lr_change[valid].std())
            state["lr_mean_change"] = float(lr_change[valid].mean())
            state["ipr_mean_change"] = float(ipr_change[valid].mean())
            entry["states"][str(int(r["value"]))] = state
            print(f"{r['scene']} {r['value']:+.0f}: residual std {state['residual_std']:.3f} EV, level curve R2 "
                  f"{state['r2_level_curve']:.2f}, level x relative R2 {state['r2_level_relative_table']:.2f}", flush=True)
    finally:
        server.close()
    for entry in scenes.values():
        entry.pop("_ipr0", None)
    record = {"campaign_id": campaign, "runner": "tests/study_clarity_adaptation.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"reduction": f"{REDUCTION}x area", "levels_log2": LEVELS.tolist(),
                         "residual": "Lightroom change minus IPR change of log2 luminance, each against its own neutral",
                         "valid": "neutral log2 luminance above -11, Lightroom state below white"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "scenes": scenes}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(scenes)} scènes, {record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
