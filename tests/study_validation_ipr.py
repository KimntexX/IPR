"""IPR's exposure, contrast and white balance against Lightroom's, state by state, in memory.

Beta validation campaigns of 28 October 2026. For each RAW at the root of base and
each Lightroom export of the control's folder, IPR renders the same state with
iprRenderServer; Lightroom's state is read in the TIFF's embedded settings, never
in its name. Both engines are compared through their own neutral: the full
definition export of base/neutre for exposure and contrast, the 5200 K / tint 0
export of couleur/temperature for white balance (the manual anchor of the colour
protocol, IPR rendering its 5200 K state). Images are reduced by area to a long
side of 1024 pixels, IPR's to Lightroom's geometry. The change in Oklab against
the neutral is compared pixel to pixel: share of Lightroom's change left
unexplained per export, pooled per control and value (summed squares), and the
IPR/Lightroom amplitude ratio. Pixels with a channel above 0.98 in any of the
four images are left out. Writes one immutable, versioned JSON.

Usage: python tests/study_validation_ipr.py --data-version N [--control exposure ...] [--scene STEM ...]
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
from study_grading_response import oklab  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

# control: (folder, Lightroom field, IPR feature)
CONTROLS = {"exposure": ("lumiere/exposure", "Exposure2012", "exposure"),
            "contrast": ("lumiere/contrast", "Contrast2012", "contrast"),
            "temperature": ("couleur/temperature", "Temperature", "temperature"),
            "tint": ("couleur/nuance", "Tint", "tint")}
WHITE_BALANCE = ("temperature", "tint")
LONG_SIDE = 1024


def reduce(rgb, shape=None):
    if shape is None:
        scale = LONG_SIDE / max(rgb.shape[:2])
        shape = (round(rgb.shape[0] * scale), round(rgb.shape[1] * scale))
    return cv2.resize(rgb.astype(np.float32), (shape[1], shape[0]), interpolation=cv2.INTER_AREA)


def size_of(path):
    image = cv2.imread(str(path), cv2.IMREAD_UNCHANGED | cv2.IMREAD_REDUCED_COLOR_8)
    return None if image is None else image.shape[:2]


def lr_neutral(base, scene, control, size):
    """Lightroom's neutral of the same definition as the export: base/neutre for
    the lights, the 5200 K / tint 0 anchor of couleur/temperature for white balance."""
    if control in WHITE_BALANCE:
        for p in sorted((base / "couleur/temperature").glob(f"{scene}*.tif")):
            if p.stem.split("-")[0] != scene:
                continue
            xmp = embedded_metadata(p)[1]
            if xmp.get("Temperature") == "5200" and float(xmp.get("Tint", "nan")) == 0:
                return p
        return None
    found = [p for p in sorted((base / "neutre").glob(f"{scene}*.tif"))
             if p.stem.split("-")[0] == scene and size_of(p) == size]
    return found[0] if len(found) == 1 else None


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--control", nargs="+", choices=sorted(CONTROLS), default=sorted(CONTROLS))
    parser.add_argument("--scene", nargs="+")
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-validation-ipr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    raws = {p.stem: p for p in sources(args.base, args.scene)}

    start = time.monotonic()
    rows, skipped = [], []
    with RenderServer(args.server or find_server(), timeout=600) as server:
        for scene, raw in raws.items():
            ipr_neutrals = {}
            for control in args.control:
                folder, field, feature = CONTROLS[control]
                exports = [p for p in sorted((args.base / folder).glob(f"{scene}*.tif")) if p.stem.split("-")[0] == scene]
                states = {}
                for p in exports:
                    value = float(embedded_metadata(p)[1].get(field, "nan"))
                    if control == "temperature" and value == 5200:
                        continue   # the anchor itself
                    if value in states:
                        raise SystemExit(f"Two exports of {control} {value} for {scene}: {states[value]}, {p}")
                    states[value] = p
                if not states:
                    continue
                neutral = lr_neutral(args.base, scene, control, size_of(next(iter(states.values()))))
                if neutral is None:
                    skipped.append({"scene": scene, "control": control, "reason": "no single Lightroom neutral"})
                    continue
                lr0 = reduce(linear(read_tiff(neutral)))
                shape = lr0.shape[:2]
                reference = ("temperature", 5200) if control in WHITE_BALANCE else ("neutral", 0)
                if reference not in ipr_neutrals:
                    ipr_neutrals[reference] = reduce(linear(server.render(raw, *reference)[0]), shape)
                ipr0 = ipr_neutrals[reference]
                lab_lr0, lab_ipr0 = oklab(lr0), oklab(ipr0)
                for value, path in sorted(states.items()):
                    lr1 = reduce(linear(read_tiff(path)), shape)
                    ipr_value = int(round(value)) if control != "exposure" else value
                    ipr1 = reduce(linear(server.render(raw, feature, ipr_value)[0]), shape)
                    ok = (lr0.max(2) < .98) & (lr1.max(2) < .98) & (ipr0.max(2) < .98) & (ipr1.max(2) < .98)
                    d_lr, d_ipr = (oklab(lr1) - lab_lr0)[ok], (oklab(ipr1) - lab_ipr0)[ok]
                    lr_sq, err_sq = float(np.sum(d_lr ** 2)), float(np.sum((d_lr - d_ipr) ** 2))
                    rows.append({"scene": scene, "control": control, "value": value,
                                 "lr_file": str(path.relative_to(args.base)), "lr_neutral": str(neutral.relative_to(args.base)),
                                 "kept_share": float(ok.mean()), "unexplained": err_sq / max(lr_sq, 1e-12),
                                 "lr_change_sq": lr_sq, "error_sq": err_sq,
                                 "lr_change_rms": float(np.sqrt(np.mean(np.sum(d_lr ** 2, 1)))),
                                 "ipr_change_rms": float(np.sqrt(np.mean(np.sum(d_ipr ** 2, 1)))),
                                 "lr_mean_change": [float(x) for x in d_lr.mean(0)],
                                 "ipr_mean_change": [float(x) for x in d_ipr.mean(0)]})
                    r = rows[-1]
                    print(f"{scene} {control} {value:g}: unexplained {r['unexplained']:.3f}, "
                          f"IPR/LR {r['ipr_change_rms'] / max(r['lr_change_rms'], 1e-9):.2f}", flush=True)
    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        groups[r["control"]].append(r)
        groups[f"{r['control']} {r['value']:g}"].append(r)
    for group, members in sorted(groups.items()):
        values = [r["unexplained"] for r in members]
        summary[group] = {"pooled": sum(r["error_sq"] for r in members) / max(sum(r["lr_change_sq"] for r in members), 1e-12),
                          "median": float(np.median(values)), "p90": float(np.quantile(values, .9)), "exports": len(values),
                          "ipr_over_lr_rms": float(np.median([r["ipr_change_rms"] / max(r["lr_change_rms"], 1e-9)
                                                              for r in members]))}
    record = {"campaign_id": campaign, "runner": "tests/study_validation_ipr.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "scenes": sorted(raws), "controls": args.control,
              "metric": "share of Lightroom's Oklab change against its neutral left unexplained by IPR's",
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "skipped": skipped, "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for group, s in summary.items():
        print(f"{group}: pooled {s['pooled']:.3f}, median {s['median']:.3f}, p90 {s['p90']:.3f}, "
              f"IPR/LR {s['ipr_over_lr_rms']:.2f} ({s['exports']})")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
