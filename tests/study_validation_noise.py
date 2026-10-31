"""IPR's noise reduction against Lightroom's, state by state, in memory.

Beta validation of the noise reduction, 31 October 2026. For each RAW at the root
of base, each Lightroom export of base/bruit is read with its embedded settings
(luminance, colour, sharpening), and IPR renders the same state with
iprRenderServer along both of its paths: the current denoise (after the tones)
and the camera-signal denoise (before the profiles). Each engine is compared
with its own neutral at full definition (Lightroom: base/neutre, noise
reduction and sharpening at 0; IPR: same strengths at 0, sharpening 0), on
tiles chosen on that neutral, so no registration between engines is needed:
the flattest tiles for the noise (Haar HH and mid band G1 - G4 sigma of
luminance and chroma) and the most structured ones for the detail (RMS of the
fine band Y - G1 and the mid band G1 - G4). Per state, the ratio state /
neutral of each measure is the engine's factor; IPR's factor is compared with
Lightroom's. Writes one immutable, versioned JSON.

Usage: python tests/study_validation_noise.py --data-version N [--scene STEM ...] [--path current camera]
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

TILE = 64
FLAT_FRACTION, TEXTURED_FRACTION = 0.15, 0.20
MAX_TILES = 400
WEIGHTS = np.array([.2126, .7152, .0722], dtype=np.float32)
MEASURES = ("luma", "chroma", "luma_mid", "chroma_mid", "fine", "mid")
NO_SHARPENING = {"amount": 0, "radius": 1.0, "masking": 0}


def linear(pixels):
    v = pixels.astype(np.float32) / 65535.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4).astype(np.float32)


def lr_state(path):
    x = embedded_metadata(path)[1]
    number = lambda key: int(round(float(x.get(key) or 0)))
    sharpening = {"amount": number("Sharpness"), "radius": float(x.get("SharpenRadius") or 1.0),
                  "masking": number("SharpenEdgeMasking")}
    return number("LuminanceSmoothing"), number("ColorNoiseReduction"), sharpening


def choose_tiles(rgb):
    """Positions of the flattest and of the most structured unclipped tiles of a neutral."""
    h, w = rgb.shape[0] // TILE * TILE, rgb.shape[1] // TILE * TILE
    y = rgb[:h, :w] @ WEIGHTS
    blocks = y.reshape(h // TILE, TILE, w // TILE, TILE).swapaxes(1, 2)
    level = blocks.mean((2, 3))
    clipped = rgb[:h, :w].max(2).reshape(h // TILE, TILE, w // TILE, TILE).swapaxes(1, 2).max((2, 3)) > .98
    smooth = cv2.GaussianBlur(y, (0, 0), 3.0)
    structure = smooth.reshape(h // TILE, TILE, w // TILE, TILE).swapaxes(1, 2).std((2, 3)) / np.maximum(level, 1e-4)
    valid = ~clipped & (level > .003) & (level < .8)
    rows, cols = np.nonzero(valid)
    s = structure[rows, cols]
    order = np.argsort(s)
    n = len(order)
    flat = order[:int(n * FLAT_FRACTION)]
    textured = order[n - int(n * TEXTURED_FRACTION):]
    pick = lambda idx: [(int(rows[i]) * TILE, int(cols[i]) * TILE) for i in idx[np.linspace(0, len(idx) - 1, min(MAX_TILES, len(idx))).astype(int)]] if len(idx) else []
    return {"flat": pick(flat), "textured": pick(textured)}


def measure(rgb, positions):
    """Per tile: noise of the flat tiles, detail bands of the textured ones."""
    out = {}
    for y0, x0 in positions["flat"]:
        t = rgb[y0:y0 + TILE, x0:x0 + TILE]
        Y = t @ WEIGHTS
        co, cg = t[..., 0] - t[..., 2], t[..., 1] - (t[..., 0] + t[..., 2]) / 2
        out.setdefault("luma", []).append(haar_sigma(Y))
        out.setdefault("chroma", []).append(float(np.hypot(haar_sigma(co), haar_sigma(cg)) / np.sqrt(2)))
        out.setdefault("luma_mid", []).append(mid_sigma(Y))
        out.setdefault("chroma_mid", []).append(float(np.hypot(mid_sigma(co), mid_sigma(cg)) / np.sqrt(2)))
    for y0, x0 in positions["textured"]:
        Y = rgb[y0:y0 + TILE, x0:x0 + TILE] @ WEIGHTS
        g1 = cv2.GaussianBlur(Y, (0, 0), 1.0, borderType=cv2.BORDER_REFLECT)
        g4 = cv2.GaussianBlur(Y, (0, 0), 4.0, borderType=cv2.BORDER_REFLECT)
        out.setdefault("fine", []).append(float(np.sqrt(np.mean((Y - g1)[8:-8, 8:-8] ** 2))))
        out.setdefault("mid", []).append(float(np.sqrt(np.mean((g1 - g4)[8:-8, 8:-8] ** 2))))
    return {k: np.array(v) for k, v in out.items()}


def factors(state, neutral):
    """Median per-tile ratio state / neutral of each measure."""
    return {k: float(np.median(state[k] / np.maximum(neutral[k], 1e-9))) for k in neutral if k in state}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--scene", nargs="+")
    parser.add_argument("--path", nargs="+", choices=("current", "camera"), default=["current", "camera"])
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--server", type=Path)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-validation-bruit-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))

    start = time.monotonic()
    rows, skipped = [], []
    with RenderServer(args.server or find_server(), timeout=900) as server:
        for raw in sources(args.base, args.scene):
            scene = raw.stem
            exports = [p for p in sorted((args.base / "bruit").glob(f"{scene}*.tif")) if p.stem.split("-")[0] == scene]
            neutral_path = args.base / "neutre" / f"{scene}.tif"
            if not exports or not neutral_path.is_file():
                skipped.append({"scene": scene, "reason": "no Lightroom noise exports or full definition neutral"})
                continue
            states = {}
            for p in exports:
                key = lr_state(p)
                key = (key[0], key[1], tuple(sorted(key[2].items())))
                if key in states:
                    raise SystemExit(f"Two exports of the same state for {scene}: {states[key]}, {p}")
                states[key] = p
            lr0 = linear(read_tiff(neutral_path))
            lr_tiles = choose_tiles(lr0)
            lr_neutral = measure(lr0, lr_tiles)
            del lr0
            ipr_neutral = {}
            for path in args.path:
                options = {"camera_denoise": {"luminance": 0, "chrominance": 0}} if path == "camera" else {}
                rgb = linear(server.render(raw, "neutral", 0, sharpening=NO_SHARPENING, **options)[0])
                tiles = choose_tiles(rgb)
                ipr_neutral[path] = (tiles, measure(rgb, tiles))
                del rgb
            for (lum, col, sharp), path_lr in sorted(states.items()):
                sharpening = dict(sharp)
                lr = factors(measure(linear(read_tiff(path_lr)), lr_tiles), lr_neutral)
                row = {"scene": scene, "lr_file": path_lr.name, "lr_sha256": sha256(path_lr),
                       "luminance": lum, "colour": col, "sharpening": sharpening, "lr": lr, "ipr": {}}
                for path in args.path:
                    strengths = {"luminance": lum, "chrominance": col}
                    options = {"camera_denoise": strengths} if path == "camera" else {"denoise": strengths}
                    rgb = linear(server.render(raw, "neutral", 0, sharpening=sharpening, **options)[0])
                    tiles, neutral = ipr_neutral[path]
                    row["ipr"][path] = factors(measure(rgb, tiles), neutral)
                    del rgb
                rows.append(row)
                gaps = " ".join(f"{p} luma {row['ipr'][p]['luma']:.2f}/{lr['luma']:.2f} chroma {row['ipr'][p]['chroma']:.2f}/{lr['chroma']:.2f} "
                                f"fine {row['ipr'][p]['fine']:.2f}/{lr['fine']:.2f}" for p in args.path)
                print(f"{scene} L{lum} C{col} S{sharpening['amount']}: {gaps}", flush=True)

    summary = {}
    groups = collections.defaultdict(list)
    for r in rows:
        groups[f"L{r['luminance']} C{r['colour']} S{r['sharpening']['amount']}"].append(r)
    for group, members in sorted(groups.items()):
        summary[group] = {"exports": len(members),
                          "lr": {m: float(np.median([r["lr"][m] for r in members])) for m in MEASURES},
                          **{path: {m: {"median_factor": float(np.median([r["ipr"][path][m] for r in members])),
                                        "median_log2_gap": float(np.median([np.log2(r["ipr"][path][m] / r["lr"][m]) for r in members])),
                                        "p90_abs_log2_gap": float(np.quantile([abs(np.log2(r["ipr"][path][m] / r["lr"][m])) for r in members], .9))}
                                    for m in MEASURES} for path in args.path}}
    record = {"campaign_id": campaign, "runner": "tests/study_validation_noise.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"tile": TILE, "flat_fraction": FLAT_FRACTION, "textured_fraction": TEXTURED_FRACTION,
                         "max_tiles": MAX_TILES, "space": "linear sRGB from 16-bit sRGB encoding, full definition",
                         "noise": "Haar HH and mid band (G1 - G4) MAD sigma; chroma = hypot(Co, Cg)/sqrt 2",
                         "detail": "RMS of Y - G1 (fine) and G1 - G4 (mid) on the most structured tiles",
                         "factor": "median per-tile ratio state / own neutral, tiles chosen on each engine's neutral",
                         "paths": {"current": "denoise after the tones", "camera": "camera-signal denoise before the profiles"},
                         "unmatched": "Lightroom's luminance detail/contrast and colour detail/smoothness stay at their defaults; "
                                      "IPR has no sharpening detail control (Lightroom 25)"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "skipped": skipped, "summary": summary, "rows": rows}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    for group, s in summary.items():
        print(group, "LR", {m: round(v, 2) for m, v in s["lr"].items()})
        for path in args.path:
            print("   ", path, {m: round(v["median_factor"], 2) for m, v in s[path].items()})
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
