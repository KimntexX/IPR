"""Response of the effects (texture, clarity, dehaze) by scale and by tone, Lightroom against IPR.

For each state, the variant is compared with its own engine's neutral, pixel to
pixel (same engine, same geometry): Lightroom state TIFF against the Lightroom
neutral, IPR state against the IPR neutral, both IPR images rendered in memory
by iprRenderServer. On the logarithm of the luminance (the perceptual domain of
local contrast), the image is split into difference-of-Gaussian bands; the gain
of each band is the least-squares slope of the variant's band on the neutral's.
The tone response is the median change of log luminance per neutral level, and
the gain of the 2-16 pixel bands is also given per tone. Writes one immutable,
versioned JSON.

Usage: python tests/study_effects_response.py --data-version N [--feature texture ...] [--scene STEM ...]
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
SIGMAS = (1, 2, 4, 8, 16, 32, 64, 128, 256)
BANDS = ("fine", "1-2", "2-4", "4-8", "8-16", "16-32", "32-64", "64-128", "128-256", "256+")
TONES = np.array([0.003, 0.02, 0.08, 0.25, 0.6, 1.0])   # linear luminance bins of the neutral
FEATURES = ("texture", "clarity", "dehaze")


def log_luma(rgb):
    return np.log2(np.clip(rgb @ WEIGHTS, 1e-4, None)).astype(np.float32)


def gaussian(l, sigma):
    """Gaussian blur; beyond sigma 16 at reduced resolution (area reduction, sigma 4, bilinear
    enlargement), which keeps 256-pixel scales tractable on 32 Mpx images."""
    if sigma <= 16:
        return cv2.GaussianBlur(l, (0, 0), sigma, borderType=cv2.BORDER_REFLECT)
    f = sigma // 4
    small = cv2.resize(l, (max(1, l.shape[1] // f), max(1, l.shape[0] // f)), interpolation=cv2.INTER_AREA)
    small = cv2.GaussianBlur(small, (0, 0), 4, borderType=cv2.BORDER_REFLECT)
    return cv2.resize(small, (l.shape[1], l.shape[0]), interpolation=cv2.INTER_LINEAR)


def bands(l):
    blurred = [l] + [gaussian(l, s) for s in SIGMAS]
    return [blurred[i] - blurred[i + 1] for i in range(len(SIGMAS))] + [blurred[-1]]


def response(neutral, variant):
    """Band gains, tone response and per-tone mid-band gains of variant against neutral."""
    y0 = neutral @ WEIGHTS
    valid = (y0 > 0.002) & (neutral.max(axis=2) < 0.98) & (variant.max(axis=2) < 0.98)
    valid = cv2.erode(valid.astype(np.uint8), np.ones((5, 5), np.uint8)) > 0
    l0, l1 = log_luma(neutral), log_luma(variant)
    b0, b1 = bands(l0), bands(l1)
    result = {"valid_fraction": float(valid.mean()), "band_gain": {}, "tone": [], "mid_gain_by_tone": []}
    for name, a, b in zip(BANDS, b0, b1):
        if name == "256+":
            continue
        a, b = a[valid], b[valid]
        result["band_gain"][name] = float(np.dot(a, b) / max(np.dot(a, a), 1e-12))
    mid0 = b0[2] + b0[3] + b0[4]   # 2-16 pixels
    mid1 = b1[2] + b1[3] + b1[4]
    for lo, hi in zip(TONES[:-1], TONES[1:]):
        sel = valid & (y0 >= lo) & (y0 < hi)
        if sel.sum() < 2000:
            continue
        a, b = mid0[sel], mid1[sel]
        result["tone"].append({"level": float(np.sqrt(lo * hi)), "pixels": int(sel.sum()),
                               "delta_ev": float(np.median((l1 - l0)[sel]))})
        result["mid_gain_by_tone"].append({"level": float(np.sqrt(lo * hi)),
                                           "gain": float(np.dot(a, b) / max(np.dot(a, a), 1e-12))})
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--feature", action="append", default=[])
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path)
    args = parser.parse_args(argv)
    features = args.feature or list(FEATURES)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-reponse-effets-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    records = [r for r in inputs.base_records(args.base, ["effets"])
               if r["feature"] in features and r.get("reference") and (not args.scene or r["scene"] in args.scene)]
    raws = {p.stem: p for p in sources(args.base, [])}
    start = time.monotonic()
    states = []
    server = RenderServer(args.server or find_server(), timeout=300)
    try:
        neutrals = {}
        for r in sorted(records, key=lambda r: (r["scene"], r["feature"], r["value"])):
            raw = raws.get(r["scene"])
            if raw is None:
                continue
            lr = response(linear(read_tiff(r["reference"])), linear(read_tiff(r["file"])))
            if raw not in neutrals:
                neutrals.clear()
                neutrals[raw] = linear(server.render(raw, "neutral", 0)[0])
            pixels, _ = server.render(raw, r["feature"], int(r["value"]))
            ipr = response(neutrals[raw], linear(pixels))
            states.append({"scene": r["scene"], "feature": r["feature"], "value": r["value"],
                           "lr_state": Path(r["file"]).name, "lr_neutral": Path(r["reference"]).name,
                           "lr": lr, "ipr": ipr})
            g = lambda x: " ".join(f"{x['band_gain'][b]:.2f}" for b in BANDS[:-1])  # noqa: E731
            print(f"{r['scene']} {r['feature']} {r['value']:+.0f}\n  LR  {g(lr)}\n  IPR {g(ipr)}", flush=True)
    finally:
        server.close()
    record = {"campaign_id": campaign, "runner": "tests/study_effects_response.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": str(server.executable), "sha256": server.sha256},
              "method": {"domain": "log2 of linear sRGB luminance (Rec.709 weights)",
                         "large_scales": "sigma above 16 computed at 1/(sigma/4) resolution",
                         "bands": "difference of Gaussians, sigma " + ", ".join(map(str, SIGMAS)) + " native pixels",
                         "gain": "least-squares slope of the variant band on the neutral band, valid pixels",
                         "valid": "neutral luminance > 0.002, no channel >= 0.98 in either image, eroded 5x5",
                         "tone": "median log2 change per neutral luminance bin " + str(TONES.tolist()),
                         "mid_gain_by_tone": "gain of the 2-16 pixel bands per neutral luminance bin"},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "states": states}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{len(states)} états, {record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
