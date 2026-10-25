"""Noise of the IPR neutral against the Lightroom neutral (noise reduction 0).

Each RAW of the base root is rendered in memory by iprRenderServer (neutral
state) and compared with its Lightroom neutral export (base/neutre/STEM.tif,
noise reduction at 0 by protocol). The engines are not registered: on each
image the flattest tiles are kept, grouped by luminance level, and the Haar HH
noise of luminance and chroma is compared level by level (same tile size,
linear sRGB). The ratio IPR / LR per scene is the gap the demosaic must close.
Writes one immutable, versioned JSON.

Usage: python tests/study_lr_neutral_noise.py --data-version N [--scene STEM ...] [--base tests/image/base]
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
from analysis.metrics import blur, haar_sigma  # noqa: E402
from analysis.native import sha256  # noqa: E402
from analysis.render import RenderServer, find_server  # noqa: E402
from study_sources import git, sources  # noqa: E402

TILE = 64
FLAT_FRACTION = 0.15        # flattest tiles kept per image
LEVELS = np.geomspace(0.003, 0.8, 13)   # luminance bins (linear Y)
WEIGHTS = np.array([.2126, .7152, .0722])


def linear(pixels):
    v = pixels.astype(np.float64) / 65535.0
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def read_tiff(path):
    """16-bit RGB of a TIFF (PIL would truncate 16-bit RGB to 8 bits)."""
    bgr = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if bgr is None or bgr.dtype != np.uint16 or bgr.ndim != 3:
        raise ValueError("Expected a 16-bit RGB TIFF: " + str(path))
    return bgr[..., 2::-1]


def mid_sigma(plane):
    """Robust sigma of the mid band G1 - G4: the visible grain, coarser than the Haar HH."""
    p = plane.astype(np.float32)
    band = cv2.GaussianBlur(p, (0, 0), 1.0, borderType=cv2.BORDER_REFLECT) -         cv2.GaussianBlur(p, (0, 0), 4.0, borderType=cv2.BORDER_REFLECT)
    inner = band[8:-8, 8:-8]
    return float(np.median(np.abs(inner - np.median(inner))) / .6744897501960817)


def tiles(rgb):
    """Per tile: level, flatness, luma and chroma Haar noise, for unclipped tiles."""
    out = []
    h, w = rgb.shape[0] // TILE * TILE, rgb.shape[1] // TILE * TILE
    for y in range(0, h, TILE):
        for x in range(0, w, TILE):
            t = rgb[y:y + TILE, x:x + TILE]
            if t.max() > 0.98:
                continue
            Y = t @ WEIGHTS
            level = float(Y.mean())
            if level < LEVELS[0] or level > LEVELS[-1]:
                continue
            smooth = blur(blur(Y))
            sy = haar_sigma(Y)
            co, cg = t[..., 0] - t[..., 2], t[..., 1] - (t[..., 0] + t[..., 2]) / 2
            out.append((level, float(smooth.std()) / level, sy, float(np.hypot(haar_sigma(co), haar_sigma(cg)) / np.sqrt(2)),
                        mid_sigma(Y), float(np.hypot(mid_sigma(co), mid_sigma(cg)) / np.sqrt(2))))
    return np.array(out)


def profile(rgb):
    t = tiles(rgb)
    if len(t) == 0:
        return {}
    keep = t[t[:, 1] <= np.quantile(t[:, 1], FLAT_FRACTION)]
    bins = {}
    for lo, hi in zip(LEVELS[:-1], LEVELS[1:]):
        sel = keep[(keep[:, 0] >= lo) & (keep[:, 0] < hi)]
        if len(sel) >= 5:
            bins[f"{np.sqrt(lo * hi):.4f}"] = {"tiles": int(len(sel)), "luma": float(np.median(sel[:, 2])),
                                               "chroma": float(np.median(sel[:, 3])),
                                               "luma_mid": float(np.median(sel[:, 4])),
                                               "chroma_mid": float(np.median(sel[:, 5]))}
    return bins


def levels(rgb):
    """Percentiles 10, 25, 50, 75 and 90 of the linear luminance, on a 1/16 sample:
    a global brightness comparison that needs no registration."""
    y = rgb[::4, ::4] @ WEIGHTS
    return {str(q): float(np.percentile(y, q)) for q in (10, 25, 50, 75, 90)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--extra-base", type=Path, action="append", default=[], help="other RAW folders (neutrals stay in BASE/neutre)")
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path, help="iprRenderServer (default: the Release build of the working tree)")
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-bruit-neutre-lr-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    raws, seen = [], set()
    for p in sources([args.base, *args.extra_base], args.scene):   # base first: a duplicate elsewhere is skipped
        if p.stem not in seen and (args.base / "neutre" / f"{p.stem}.tif").is_file():
            raws.append(p)
            seen.add(p.stem)
    start = time.monotonic()
    scenes = []
    server = RenderServer(args.server or find_server(), timeout=300)
    try:
        for raw in raws:
            reference = args.base / "neutre" / f"{raw.stem}.tif"
            pixels, metadata = server.render(raw, "neutral", 0)
            rgb = linear(pixels)
            del pixels
            ipr, ipr_levels = profile(rgb), levels(rgb)
            del rgb
            rgb = linear(read_tiff(reference))
            lr, lr_levels = profile(rgb), levels(rgb)
            del rgb
            common = sorted(set(ipr) & set(lr))
            ratios = {k: {f: ipr[k][f] / lr[k][f] for f in ("luma", "chroma", "luma_mid", "chroma_mid")} for k in common}
            scene = {"raw": raw.name, "raw_sha256": metadata["declaration"].get("source_sha256"),
                     "lr_neutral": reference.name, "lr_neutral_sha256": sha256(reference),
                     "ipr": ipr, "lr": lr, "ratio_by_level": ratios,
                     "median_luma_ratio": float(np.median([r["luma"] for r in ratios.values()])) if ratios else None,
                     "median_chroma_ratio": float(np.median([r["chroma"] for r in ratios.values()])) if ratios else None,
                     "median_luma_mid_ratio": float(np.median([r["luma_mid"] for r in ratios.values()])) if ratios else None,
                     "median_chroma_mid_ratio": float(np.median([r["chroma_mid"] for r in ratios.values()])) if ratios else None,
                     "luminance_percentiles": {"ipr": ipr_levels, "lr": lr_levels}}
            scenes.append(scene)
            print(f"{raw.name}: luma {scene['median_luma_ratio']}, chroma {scene['median_chroma_ratio']}", flush=True)
    finally:
        server.close()
    record = {"campaign_id": campaign, "runner": "tests/study_lr_neutral_noise.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"tile": TILE, "flat_fraction": FLAT_FRACTION, "levels": LEVELS.tolist(),
                         "space": "linear sRGB from 16-bit sRGB encoding",
                         "noise": "Haar HH and mid band (G1 - G4) MAD sigma; chroma = hypot(Co, Cg)/sqrt 2",
                         "registration": "none: flattest tiles of each engine, compared per luminance level"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "scenes": scenes}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(scenes)} scènes, {record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
