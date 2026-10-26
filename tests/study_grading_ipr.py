"""IPR's colour grading against Lightroom's, state by state, rendered in memory.

For each scene and state (zone, hue, saturation), IPR renders the neutral and the
state with iprRenderServer (grading override), both reduced by area to the
geometry of Lightroom's 2048-pixel export and then by 2, as the response study.
The change in Oklab against the neutral of the same engine is compared pixel to
pixel with Lightroom's: share of Lightroom's change left unexplained, and the
chroma and lightness of the change at the zone's peak. Pixels with a channel above
0.98 in a neutral or a state are left out. Writes one immutable, versioned JSON.

With --fusion, the states are those of grading/fusion (blending, balance and
zone combinations); with --luminance, those of grading/luminance (each zone's and
the global wheel's luminance, the global wheel's colour, luminance with a colour);
each rendered with its exact grading.

Usage: python tests/study_grading_ipr.py --data-version N [--scene STEM ...] [--fusion | --luminance]
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
from analysis.native import embedded_metadata  # noqa: E402
from analysis.render import RenderServer, find_server  # noqa: E402
from study_grading_luminance import ZONES as LUMINANCE_ZONES  # noqa: E402
from study_grading_response import ZONES, oklab  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

STATES = [(h, 50) for h in range(0, 360, 30)] + [(30, 100), (210, 100)]
DEFAULT_SCENES = ("6V1A0007", "6V1A0327", "6V1A9539", "6V1A0043", "IMG_7953", "6V1A9141", "IMG_7958",
                  "7Y6A7599", "IMG_6954", "IMG_4002")


def reduce_to(rgb, shape):
    small = cv2.resize(rgb.astype(np.float32), (shape[1], shape[0]), interpolation=cv2.INTER_AREA)
    return cv2.resize(small, None, fx=.5, fy=.5, interpolation=cv2.INTER_AREA)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--scene", action="append")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--fusion", action="store_true", help="the blending and balance states of grading/fusion")
    parser.add_argument("--luminance", action="store_true", help="the luminance and global states of grading/luminance")
    args = parser.parse_args(argv)
    if args.fusion and args.luminance:
        raise SystemExit("--fusion or --luminance, not both")
    mode = "-fusion" if args.fusion else "-luminance" if args.luminance else ""
    stamp = datetime.now(timezone.utc)
    campaign = (f"etude-grading-ipr{mode}-v{args.data_version:03d}-"
                f"{stamp.strftime('%Y%m%dT%H%M%SZ')}")
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    scenes = args.scene or list(DEFAULT_SCENES)
    root = args.base / "grading"
    raws = {p.stem: p for p in sources(args.base, [])}
    lr = collections.defaultdict(dict)
    for zone, (hue_field, saturation_field) in ZONES.items():
        for p in sorted((root / zone).glob("*.tif")):
            scene = p.stem.split("-")[0]
            if scene in scenes:
                _, xmp = embedded_metadata(p)
                lr[scene][(zone, int(xmp[hue_field]), int(xmp[saturation_field]))] = p
    neutral = {p.stem.split("-")[0]: p for p in (root / "neutre").glob("*.tif")}
    fields = [(zone, h, sat) for zone, (h, sat) in ZONES.items()]

    def states_of(scene):
        if args.luminance:
            seen = set()
            for path in sorted((root / "luminance").glob(scene + "*.tif")):
                if path.stem.split("-")[0] != scene:
                    continue
                _, xmp = embedded_metadata(path)
                grading = {"hue": [int(xmp.get(h, 0)) for _, _, h, _ in LUMINANCE_ZONES],
                           "saturation": [int(xmp.get(s, 0)) for _, _, _, s in LUMINANCE_ZONES],
                           "luminance": [int(xmp.get(lum, 0)) for _, lum, _, _ in LUMINANCE_ZONES]}
                key = json.dumps(grading, sort_keys=True)
                if key in seen:
                    continue
                seen.add(key)
                z = next(i for i in range(4) if grading["saturation"][i] or grading["luminance"][i])
                parts = [LUMINANCE_ZONES[z][0]]
                if grading["luminance"][z]:
                    parts.append(f"L{grading['luminance'][z]:+d}")
                if grading["saturation"][z]:
                    parts.append(f"T{grading['hue'][z]:03d}/S{grading['saturation'][z]:03d}")
                yield " ".join(parts), grading, path
            return
        if not args.fusion:
            for zone_index, zone in enumerate(ZONES):
                for hue, saturation in STATES:
                    path = lr[scene].get((zone, hue, saturation))
                    if path is not None:
                        grading = {"hue": [0, 0, 0], "saturation": [0, 0, 0]}
                        grading["hue"][zone_index], grading["saturation"][zone_index] = hue, saturation
                        yield f"{zone} {hue}/{saturation}", grading, path
            return
        seen = set()
        for path in sorted((root / "fusion").glob(scene + "*.tif")):
            if path.stem.split("-")[0] != scene:
                continue
            _, xmp = embedded_metadata(path)
            grading = {"hue": [int(xmp[h]) for _, h, _ in fields], "saturation": [int(xmp[s]) for _, _, s in fields],
                       "blending": int(xmp["ColorGradeBlending"]), "balance": int(xmp["SplitToningBalance"])}
            key = json.dumps(grading, sort_keys=True)
            if key in seen:
                continue
            seen.add(key)
            zones = "+".join(z for (z, _, _), sat in zip(fields, grading["saturation"]) if sat)
            yield f"{zones} F{grading['blending']} B{grading['balance']:+d}", grading, path

    start = time.monotonic()
    rows = []
    with RenderServer(args.server or find_server(), timeout=300) as server:
        for scene in scenes:
            lr0 = linear(read_tiff(neutral[scene])).astype(np.float32)
            shape = lr0.shape[:2]
            lr0 = cv2.resize(lr0, None, fx=.5, fy=.5, interpolation=cv2.INTER_AREA)
            ipr0 = reduce_to(linear(server.render(raws[scene], "neutral", 0)[0]), shape)
            lab_lr0, lab_ipr0 = oklab(lr0), oklab(ipr0)
            for label, grading, path in states_of(scene):
                lr1 = cv2.resize(linear(read_tiff(path)).astype(np.float32), None, fx=.5, fy=.5, interpolation=cv2.INTER_AREA)
                ipr1 = reduce_to(linear(server.render(raws[scene], "neutral", 0, grading=grading)[0]), shape)
                ok = (lr0.max(2) < .98) & (lr1.max(2) < .98) & (ipr0.max(2) < .98) & (ipr1.max(2) < .98)
                d_lr, d_ipr = (oklab(lr1) - lab_lr0)[ok], (oklab(ipr1) - lab_ipr0)[ok]
                unexplained = float(np.sum((d_lr - d_ipr) ** 2) / max(np.sum(d_lr ** 2), 1e-12))
                rows.append({"scene": scene, "state": label, "grading": grading, "unexplained": unexplained,
                             "lr_mean_change": d_lr.mean(0).tolist(), "ipr_mean_change": d_ipr.mean(0).tolist()})
                print(f"{scene} {label}: unexplained {unexplained:.3f}", flush=True)
    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        zone, _, rest = r["state"].partition(" ")
        if args.luminance:
            kind = ("luminance+colour" if "L" in rest and "T" in rest else "luminance" if "L" in rest else "colour")
            groups[f"{zone} {kind}"].append(r["unexplained"])
        else:
            groups[zone if not args.fusion else zone + (" balance" if "F50 " in r["state"] and not r["state"].endswith("B+0")
                                                          else " blending")].append(r["unexplained"])
    for group, values in sorted(groups.items()):
        summary[group] = {"median": float(np.median(values)), "p90": float(np.quantile(values, .9)), "states": len(values)}
    record = {"campaign_id": campaign, "runner": "tests/study_grading_ipr.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "scenes": scenes, "states": "grading/fusion" if args.fusion else "grading/luminance" if args.luminance else STATES, "metric": "share of Lightroom's Oklab change left unexplained by IPR's",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for zone, s in summary.items():
        print(f"{zone}: median unexplained {s['median']:.3f}, 90th percentile {s['p90']:.3f} ({s['states']} states)")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
