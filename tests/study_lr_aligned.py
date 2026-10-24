"""Sharpness and grain of the IPR neutral against the Lightroom neutral, pixel to pixel.

Each RAW is rendered in memory by iprRenderServer (neutral state) and registered
on its Lightroom neutral export (base/neutre/STEM.tif, noise reduction 0) by an
integer shift. On the same 64-pixel tiles, in sRGB-encoded luminance (closer to
what the eye compares), the energy of three bands is compared: fine (x - G1),
1-2 (G1 - G2) and 2-4 (G2 - G4), separately on the most textured and the
flattest tiles. Ratios IPR / LR near 1 at every band mean the same sharpness
and the same grain; a lower fine band with a higher 1-2 band is a softer,
coarser rendering. Writes one immutable, versioned JSON.

Usage: python tests/study_lr_aligned.py --data-version N [--scene STEM ...] [--extra-base DIR]
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
from analysis.native import sha256  # noqa: E402
from analysis.render import RenderServer, find_server  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

TILE = 64
WEIGHTS = np.array([.2126, .7152, .0722], np.float32)
BANDS = ("fine", "1-2", "2-4")


def encoded_luma(rgb):
    y = np.clip(rgb @ WEIGHTS, 0, 1).astype(np.float32)
    return np.where(y <= 0.0031308, 12.92 * y, 1.055 * y ** (1 / 2.4) - 0.055).astype(np.float32)


def register(a, b):
    """Integer shift (dy, dx) of b on a, within +-4 pixels, on log luminance."""
    la, lb = np.log1p(a[::2, ::2] * 50), np.log1p(b[::2, ::2] * 50)
    best = None
    for oy in range(-2, 3):
        for ox in range(-2, 3):
            h, w = min(la.shape[0], lb.shape[0]) - 8, min(la.shape[1], lb.shape[1]) - 8
            e = float(np.mean((la[4:h, 4:w] - lb[4 + oy:h + oy, 4 + ox:w + ox]) ** 2))
            if best is None or e < best[0]:
                best = (e, oy * 2, ox * 2)
    _, by, bx = best
    best = None
    for oy in range(by - 1, by + 2):
        for ox in range(bx - 1, bx + 2):
            h, w = min(a.shape[0], b.shape[0]) - 16, min(a.shape[1], b.shape[1]) - 16
            ca, cb = a[8:h:3, 8:w:3], b[8 + oy:h + oy:3, 8 + ox:w + ox:3]
            if ca.shape != cb.shape:
                continue
            e = float(np.mean((np.log1p(ca * 50) - np.log1p(cb * 50)) ** 2))
            if best is None or e < best[0]:
                best = (e, oy, ox)
    return best[1], best[2]


def bands(y):
    g1, g2, g4 = (cv2.GaussianBlur(y, (0, 0), s, borderType=cv2.BORDER_REFLECT) for s in (1.0, 2.0, 4.0))
    return y - g1, g1 - g2, g2 - g4


def compare(ipr, lr):
    ya, yb = encoded_luma(ipr), encoded_luma(lr)
    oy, ox = register(ipr @ WEIGHTS, lr @ WEIGHTS)
    h, w = min(ya.shape[0], yb.shape[0] - oy) - 8, min(ya.shape[1], yb.shape[1] - ox) - 8
    ya, yb = ya[8:h, 8:w], yb[8 + oy:h + oy, 8 + ox:w + ox]
    ba, bb = bands(ya), bands(yb)
    smooth = cv2.GaussianBlur(yb, (0, 0), 3)
    structure = cv2.GaussianBlur(np.abs(cv2.Sobel(smooth, cv2.CV_32F, 1, 0)) + np.abs(cv2.Sobel(smooth, cv2.CV_32F, 0, 1)), (0, 0), 8)
    rows = []
    for y in range(0, ya.shape[0] - TILE, TILE):
        for x in range(0, ya.shape[1] - TILE, TILE):
            s = (slice(y, y + TILE), slice(x, x + TILE))
            if yb[s].mean() < 0.02 or yb[s].max() > 0.98:   # black or clipped tiles say nothing
                continue
            rows.append([float(structure[s].mean())] +
                        [float(np.sqrt((ba[i][s] ** 2).mean() / max((bb[i][s] ** 2).mean(), 1e-12))) for i in range(3)])
    rows = np.array(rows)
    textured = rows[rows[:, 0] >= np.quantile(rows[:, 0], 0.8)]
    flat = rows[rows[:, 0] <= np.quantile(rows[:, 0], 0.2)]
    return {"shift": [oy, ox], "tiles": int(len(rows)),
            "textured": {b: float(np.median(textured[:, i + 1])) for i, b in enumerate(BANDS)},
            "flat": {b: float(np.median(flat[:, i + 1])) for i, b in enumerate(BANDS)}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--extra-base", type=Path, action="append", default=[])
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path, help="iprRenderServer (default: the Release build of the working tree)")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-nettete-lr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    raws = [p for p in sources([args.base, *args.extra_base], args.scene)
            if (args.base / "neutre" / f"{p.stem}.tif").is_file()]
    start = time.monotonic()
    scenes = []
    server = RenderServer(args.server or find_server(), timeout=300)
    try:
        for raw in raws:
            reference = args.base / "neutre" / f"{raw.stem}.tif"
            pixels, metadata = server.render(raw, "neutral", 0)
            ipr = linear(pixels)
            del pixels
            result = compare(ipr, linear(read_tiff(reference)))
            del ipr
            result.update({"raw": raw.name, "raw_sha256": metadata["declaration"].get("source_sha256"),
                           "lr_neutral": reference.name, "lr_neutral_sha256": sha256(reference)})
            scenes.append(result)
            t = result["textured"]
            print(f"{raw.name}: textured fine {t['fine']:.2f} 1-2 {t['1-2']:.2f} 2-4 {t['2-4']:.2f}", flush=True)
    finally:
        server.close()
    record = {"campaign_id": campaign, "runner": "tests/study_lr_aligned.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"tile": TILE, "space": "sRGB-encoded luminance (Rec.709 weights on linear sRGB)",
                         "bands": "fine x - G1, 1-2 G1 - G2, 2-4 G2 - G4; RMS ratio IPR / LR per tile, median",
                         "tiles": "textured: top 20 % of LR structure (Sobel of G3, G8 mean); flat: bottom 20 %;"
                                  " tiles below 0.02 or above 0.98 excluded",
                         "registration": "integer shift on log luminance, both renders at native size"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "scenes": scenes}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(scenes)} scènes, {record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
