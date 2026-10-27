"""IPR's point colour against Lightroom's, state by state, rendered in memory.

For each scene and export of couleur-point, IPR renders the neutral and the state
with iprRenderServer (pointColor override): the source in Oklab estimated by the
latest point colour study for that Lightroom source, the sliders of the export.
Both are reduced by area to the geometry of Lightroom's 2048-pixel export and then
by 2, as the study. The change in Oklab against the neutral of the same engine is
compared pixel to pixel with Lightroom's: share of Lightroom's change left
unexplained. Pixels with a channel above 0.98 in a neutral or a state are left
out. Writes one immutable, versioned JSON.

Usage: python tests/study_point_color_ipr.py --data-version N [--scene STEM ...] [--study JSON]
"""
import argparse
import collections
import glob
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
from study_grading_ipr import DEFAULT_SCENES, reduce_to  # noqa: E402
from study_grading_response import oklab  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_point_color import embedded_xmp, point_state  # noqa: E402
from study_sources import git, sources  # noqa: E402


def state_label(state):
    parts = [f"{letter}{state[key]:+d}" for key, letter in (("hue", "T"), ("saturation", "S"), ("luminance", "L"),
                                                             ("variance", "V")) if state[key]]
    return " ".join(parts + [f"G{state['range']}"])


def kind(state):
    """Group of a state: the moved slider, the range series apart."""
    if state["range"] != 50:
        return "range"
    return next(key for key in ("hue", "saturation", "luminance", "variance") if state[key])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--scene", nargs="+", default=list(DEFAULT_SCENES))
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--study", type=Path)
    parser.add_argument("--server", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-couleur-point-ipr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    study_path = args.study or Path(sorted(glob.glob(str(args.output_dir / "etude-couleur-point-v*.json")))[-1])
    study = json.loads(study_path.read_text(encoding="utf-8"))["sources"]
    oklab_sources = {tuple(s["lightroom_source"]): s["oklab_source"] for s in study.values()}
    raws = {p.stem: p for p in sources(args.base, args.scene)}
    exports = collections.defaultdict(list)
    for p in sorted((args.base / "couleur-point").glob("*.tif")):
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
                state = point_state(embedded_xmp(path))
                if state is None or tuple(state["source"]) not in oklab_sources:
                    continue
                key = (state["source"], state_label(state))
                if key in seen:
                    continue
                seen.add(key)
                point = {"source": oklab_sources[tuple(state["source"])], "hue": state["hue"],
                         "saturation": state["saturation"], "luminance": state["luminance"],
                         "variance": state["variance"], "range": state["range"]}
                lr1 = cv2.resize(linear(read_tiff(path)).astype(np.float32), None, fx=.5, fy=.5,
                                 interpolation=cv2.INTER_AREA)
                ipr1 = reduce_to(linear(server.render(raws[scene], "neutral", 0, point_color=[point])[0]), shape)
                ok = (lr0.max(2) < .98) & (lr1.max(2) < .98) & (ipr0.max(2) < .98) & (ipr1.max(2) < .98)
                d_lr, d_ipr = (oklab(lr1) - lab_lr0)[ok], (oklab(ipr1) - lab_ipr0)[ok]
                unexplained = float(np.sum((d_lr - d_ipr) ** 2) / max(np.sum(d_lr ** 2), 1e-12))
                rows.append({"scene": scene, "source": list(state["source"]), "state": state_label(state),
                             "kind": kind(state), "unexplained": unexplained,
                             "lr_change_rms": float(np.sqrt(np.mean(np.sum(d_lr ** 2, 1)))),
                             "ipr_change_rms": float(np.sqrt(np.mean(np.sum(d_ipr ** 2, 1))))})
                print(f"{scene} {state['source'][0]:.3f} {rows[-1]['state']}: unexplained {unexplained:.3f}", flush=True)
    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        groups[r["kind"]].append(r)
        groups[f"source {r['source'][0]:.4f}"].append(r)
    for group, members in sorted(groups.items()):
        values = [r["unexplained"] for r in members]
        summary[group] = {"median": float(np.median(values)), "p90": float(np.quantile(values, .9)),
                          "states": len(values),
                          "ipr_over_lr_rms": float(np.median([r["ipr_change_rms"] / max(r["lr_change_rms"], 1e-9)
                                                              for r in members]))}
    record = {"campaign_id": campaign, "runner": "tests/study_point_color_ipr.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "study": study_path.name, "scenes": args.scene,
              "metric": "share of Lightroom's Oklab change left unexplained by IPR's",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for group, s in summary.items():
        print(f"{group}: median unexplained {s['median']:.3f}, 90th percentile {s['p90']:.3f}, "
              f"IPR/LR change {s['ipr_over_lr_rms']:.2f} ({s['states']} states)")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
