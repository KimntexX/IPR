"""IPR's point curves against Lightroom's, state by state, rendered in memory.

For each scene and export of courbes, IPR renders the neutral and the state with
iprRenderServer (toneCurves override, the four curves the export carries), both
reduced by area to the geometry of Lightroom's 2048-pixel export and then by 2.
The change in Oklab against the neutral of the same engine is compared pixel to
pixel with Lightroom's: share of Lightroom's change left unexplained, per export
and pooled over the exports of a group (summed squares). Pixels with a channel
above 0.98 in a neutral or a state are left out. Writes one immutable, versioned
JSON.

Usage: python tests/study_curves_ipr.py --data-version N [--scene STEM ...]
"""
import argparse
import collections
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
from analysis.render import RenderServer, find_server  # noqa: E402
from study_curves import FIELDS, changed, curves_of  # noqa: E402
from study_grading_ipr import DEFAULT_SCENES, reduce_to  # noqa: E402
from study_grading_response import oklab  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_point_color import embedded_xmp  # noqa: E402
from study_sources import git, sources  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--scene", nargs="+", default=list(DEFAULT_SCENES))
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-courbes-ipr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    raws = {p.stem: p for p in sources(args.base, args.scene)}
    exports = collections.defaultdict(list)
    for p in sorted((args.base / "courbes").glob("*.tif")):
        scene = p.stem.split("-")[0]
        if scene in raws:
            exports[scene].append(p)

    start = time.monotonic()
    rows = []
    with RenderServer(args.server or find_server(), timeout=300) as server:
        for scene in args.scene:
            lr0 = linear(read_tiff(args.base / "grading/neutre" / f"{scene}.tif")).astype(np.float32)
            shape = lr0.shape[:2]
            lr0 = cv2.resize(lr0, None, fx=.5, fy=.5, interpolation=cv2.INTER_AREA)
            ipr0 = reduce_to(linear(server.render(raws[scene], "neutral", 0)[0]), shape)
            lab_lr0, lab_ipr0 = oklab(lr0), oklab(ipr0)
            seen = set()
            for path in exports[scene]:
                curves = curves_of(embedded_xmp(path))
                field = changed(curves)
                if field is None:
                    continue
                key = (field, tuple(curves[field]))
                if key in seen:
                    continue
                seen.add(key)
                tone_curves = [[list(p) for p in curves.get(f, [])] for f in FIELDS]
                lr1 = cv2.resize(linear(read_tiff(path)).astype(np.float32), None, fx=.5, fy=.5,
                                 interpolation=cv2.INTER_AREA)
                ipr1 = reduce_to(linear(server.render(raws[scene], "neutral", 0, tone_curves=tone_curves)[0]), shape)
                ok = (lr0.max(2) < .98) & (lr1.max(2) < .98) & (ipr0.max(2) < .98) & (ipr1.max(2) < .98)
                d_lr, d_ipr = (oklab(lr1) - lab_lr0)[ok], (oklab(ipr1) - lab_ipr0)[ok]
                lr_sq, err_sq = float(np.sum(d_lr ** 2)), float(np.sum((d_lr - d_ipr) ** 2))
                label = (field.replace("ToneCurvePV2012", "") or "RVB") + " " + " ".join(
                    f"{round(x * 255)}>{round(y * 255)}" for x, y in curves[field])
                rows.append({"scene": scene, "curve": field, "state": label, "unexplained": err_sq / max(lr_sq, 1e-12),
                             "lr_change_sq": lr_sq, "error_sq": err_sq,
                             "lr_change_rms": float(np.sqrt(np.mean(np.sum(d_lr ** 2, 1)))),
                             "ipr_change_rms": float(np.sqrt(np.mean(np.sum(d_ipr ** 2, 1))))})
                print(f"{scene} {label}: unexplained {rows[-1]['unexplained']:.3f}", flush=True)
    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        groups["composite" if r["curve"] == "ToneCurvePV2012" else "channel"].append(r)
        groups[r["state"]].append(r)
    for group, members in sorted(groups.items()):
        values = [r["unexplained"] for r in members]
        summary[group] = {"pooled": sum(r["error_sq"] for r in members) / max(sum(r["lr_change_sq"] for r in members), 1e-12),
                          "median": float(np.median(values)), "p90": float(np.quantile(values, .9)), "exports": len(values),
                          "ipr_over_lr_rms": float(np.median([r["ipr_change_rms"] / max(r["lr_change_rms"], 1e-9)
                                                              for r in members]))}
    record = {"campaign_id": campaign, "runner": "tests/study_curves_ipr.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "scenes": args.scene, "metric": "share of Lightroom's Oklab change left unexplained by IPR's",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for group, s in summary.items():
        print(f"{group}: pooled {s['pooled']:.3f}, median {s['median']:.3f}, p90 {s['p90']:.3f}, "
              f"IPR/LR {s['ipr_over_lr_rms']:.2f} ({s['exports']})")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
