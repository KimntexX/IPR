"""IPR's sharpening against Lightroom's, state by state, in memory.

Beta validation of the sharpening, 31 October 2026. For each RAW at the root of
base, each Lightroom export of base/nettete is read with its embedded settings
(amount, radius, detail, masking; noise reduction at 0), and IPR renders the
same amount, radius and masking with iprRenderServer (IPR has no detail
control). Each engine is compared with its own neutral at full definition
(Lightroom: base/neutre, sharpening and noise reduction at 0), on tiles chosen
on that neutral: the most structured tiles for the detail gain per band (RMS of
Y - G1, G1 - G2 and G2 - G4), the flattest ones for the noise gain (Haar HH and
mid band G1 - G4 sigma of luminance), and on the structured tiles the overshoot
(RMS of the luminance beyond the neutral's 3 x 3 local range, against that
range). Per state, IPR's factor is compared with Lightroom's. Writes one
immutable, versioned JSON.

Usage: python tests/study_validation_sharpening.py --data-version N [--scene STEM ...]
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
from analysis.metrics import haar_sigma  # noqa: E402
from analysis.native import embedded_metadata, sha256  # noqa: E402
from analysis.render import RenderServer, find_server  # noqa: E402
from study_lr_neutral_noise import mid_sigma, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402
from study_validation_noise import TILE, WEIGHTS, choose_tiles, linear  # noqa: E402

MEASURES = ("fine", "b12", "b24", "luma", "luma_mid", "overshoot")


def lr_state(path):
    x = embedded_metadata(path)[1]
    number = lambda key: int(round(float(x.get(key) or 0)))
    return {"amount": number("Sharpness"), "radius": float(x.get("SharpenRadius") or 1.0),
            "detail": number("SharpenDetail"), "masking": number("SharpenEdgeMasking")}


def bands(Y):
    g1, g2, g4 = (cv2.GaussianBlur(Y, (0, 0), s, borderType=cv2.BORDER_REFLECT) for s in (1.0, 2.0, 4.0))
    rms = lambda b: float(np.sqrt(np.mean(b[8:-8, 8:-8] ** 2)))
    return rms(Y - g1), rms(g1 - g2), rms(g2 - g4)


def measure(rgb, positions, neutral_rgb=None):
    """Per tile: detail bands and overshoot of the structured tiles, noise of the flat ones.
    The overshoot needs the neutral's local range, so it is measured against neutral_rgb."""
    out = collections.defaultdict(list)
    for y0, x0 in positions["textured"]:
        Y = rgb[y0:y0 + TILE, x0:x0 + TILE] @ WEIGHTS
        for name, value in zip(("fine", "b12", "b24"), bands(Y)):
            out[name].append(value)
        Y0 = (neutral_rgb if neutral_rgb is not None else rgb)[y0:y0 + TILE, x0:x0 + TILE] @ WEIGHTS
        kernel = np.ones((3, 3), np.uint8)
        low, high = cv2.erode(Y0, kernel), cv2.dilate(Y0, kernel)
        beyond = np.maximum(Y - high, 0) + np.maximum(low - Y, 0)
        out["overshoot"].append(float(np.sqrt(np.mean(beyond[8:-8, 8:-8] ** 2)) / max(float(np.mean(high - low)), 1e-6)))
    for y0, x0 in positions["flat"]:
        Y = rgb[y0:y0 + TILE, x0:x0 + TILE] @ WEIGHTS
        out["luma"].append(haar_sigma(Y))
        out["luma_mid"].append(mid_sigma(Y))
    return {k: np.array(v) for k, v in out.items()}


def factors(state, neutral):
    """Median per-tile ratio state / neutral; the overshoot, zero at the neutral, stays absolute."""
    result = {k: float(np.median(state[k] / np.maximum(neutral[k], 1e-9))) for k in neutral if k != "overshoot"}
    result["overshoot"] = float(np.median(state["overshoot"]))
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--scene", nargs="+")
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-validation-nettete-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))

    start = time.monotonic()
    rows, skipped = [], []
    with RenderServer(args.server or find_server(), timeout=900) as server:
        for raw in sources(args.base, args.scene):
            scene = raw.stem
            exports = [p for p in sorted((args.base / "nettete").glob(f"{scene}*.tif")) if p.stem.split("-")[0] == scene]
            neutral_path = args.base / "neutre" / f"{scene}.tif"
            if not exports or not neutral_path.is_file():
                skipped.append({"scene": scene, "reason": "no Lightroom sharpening exports or full definition neutral"})
                continue
            states = {}
            for p in exports:
                key = tuple(sorted(lr_state(p).items()))
                if key in states:
                    raise SystemExit(f"Two exports of the same state for {scene}: {states[key]}, {p}")
                states[key] = p
            lr0 = linear(read_tiff(neutral_path))
            lr_tiles = choose_tiles(lr0)
            lr_neutral = measure(lr0, lr_tiles)
            ipr0 = linear(server.render(raw, "neutral", 0, sharpening={"amount": 0, "radius": 1.0, "masking": 0})[0])
            ipr_tiles = choose_tiles(ipr0)
            ipr_neutral = measure(ipr0, ipr_tiles)
            ipr_states = {}   # IPR has no detail control: one render per amount, radius and masking
            for key, path in sorted(states.items()):
                state = dict(key)
                lr = factors(measure(linear(read_tiff(path)), lr_tiles, lr0), lr_neutral)
                sharpening = {"amount": state["amount"], "radius": state["radius"], "masking": state["masking"]}
                render_key = tuple(sorted(sharpening.items()))
                if render_key not in ipr_states:
                    rgb = linear(server.render(raw, "neutral", 0, sharpening=sharpening)[0])
                    ipr_states[render_key] = factors(measure(rgb, ipr_tiles, ipr0), ipr_neutral)
                    del rgb
                ipr = ipr_states[render_key]
                rows.append({"scene": scene, "lr_file": path.name, "lr_sha256": sha256(path), "state": state,
                             "lr": lr, "ipr": ipr})
                print(f"{scene} A{state['amount']} R{state['radius']:g} D{state['detail']} M{state['masking']}: "
                      + " ".join(f"{m} {ipr[m]:.2f}/{lr[m]:.2f}" for m in MEASURES), flush=True)
            del lr0, ipr0

    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        s = r["state"]
        groups[f"A{s['amount']} R{s['radius']:g} D{s['detail']} M{s['masking']}"].append(r)
    for group, members in sorted(groups.items()):
        summary[group] = {"exports": len(members),
                          "lr": {m: float(np.median([r["lr"][m] for r in members])) for m in MEASURES},
                          "ipr": {m: float(np.median([r["ipr"][m] for r in members])) for m in MEASURES},
                          "median_log2_gap": {m: float(np.median([np.log2(max(r["ipr"][m], 1e-9) / max(r["lr"][m], 1e-9))
                                                                  for r in members])) for m in MEASURES}}
    record = {"campaign_id": campaign, "runner": "tests/study_validation_sharpening.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"tile": TILE, "space": "linear sRGB from 16-bit sRGB encoding, full definition",
                         "detail": "RMS of Y - G1, G1 - G2, G2 - G4 on the most structured tiles",
                         "noise": "Haar HH and mid band (G1 - G4) MAD sigma of luminance on the flattest tiles",
                         "overshoot": "RMS of the luminance beyond the neutral's 3 x 3 local range, over its mean range",
                         "factor": "median per-tile ratio state / own neutral, tiles chosen on each engine's neutral",
                         "unmatched": "IPR has no detail control: every Lightroom detail value is compared with the same IPR render"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "skipped": skipped, "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for group, s in summary.items():
        print(group, "IPR/LR", {m: f"{s['ipr'][m]:.2f}/{s['lr'][m]:.2f}" for m in MEASURES})
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
