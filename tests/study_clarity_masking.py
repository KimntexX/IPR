"""Non-linear masking (Moroney 2000) as the scene-adaptive tone of Lightroom's clarity.

The band model of IPR reproduces Lightroom's local contrast but leaves its
tonal adaptation (etude-clarte-adaptation-v001). Moroney's local colour
correction raises each value to a power set by a blurred, inverted luminance
mask, y' = y^gamma with gamma = 2^(k (c - m)): dark regions are lifted, bright
ones lowered. In log2 luminance L (white at 0) the change is (gamma - 1) L,
to first order L (alpha + beta m), linear in its two parameters, which are
shared by the scenes of a slider value.

The target is the residual of IPR: Lightroom's change of log2 luminance against
its neutral minus IPR's against its own (rendered in memory), at 1/4
resolution. Masks: Gaussian of the luminance encoded in gamma 2.2 or in log2,
over several radii. Leave-one-scene-out; the score is the share of the
variance of Lightroom's change left unexplained, before (band model alone) and
after the masking term. Writes one immutable, versioned JSON.

Usage: python tests/study_clarity_masking.py --data-version N [--cache DIR]
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
from study_clarity_adaptation import REDUCTION, reduced_log  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

RADII = (32, 64, 128, 256, 512)   # Gaussian sigma of the mask, native pixels
ENCODINGS = ("gamma", "log")


def load(args):
    """Per scene: Lightroom neutral level and, per value, Lightroom and IPR changes (reduced log2)."""
    records = [r for r in inputs.base_records(args.base, ["effets"]) if r["feature"] == "clarity" and r.get("reference")]
    raws = {p.stem: p for p in sources(args.base, [])}
    scenes = {}
    server = None
    try:
        for r in sorted(records, key=lambda r: (r["scene"], r["value"])):
            raw = raws.get(r["scene"])
            if raw is None:
                continue
            cached = args.cache / f"{r['scene']}_{int(r['value']):+d}.npz" if args.cache else None
            if cached and cached.exists():
                z = np.load(cached)
                level, lr, ipr, lr1 = z["level"], z["lr"], z["ipr"], z["lr1"]
            else:
                if server is None:
                    server = RenderServer(args.server or find_server(), timeout=300)
                lr0 = reduced_log(linear(read_tiff(r["reference"])))
                lr1 = reduced_log(linear(read_tiff(r["file"])))
                ipr0 = reduced_log(linear(server.render(raw, "neutral", 0)[0]))
                ipr1 = reduced_log(linear(server.render(raw, "clarity", int(r["value"]))[0]))
                h, w = min(lr0.shape[0], ipr0.shape[0]), min(lr0.shape[1], ipr0.shape[1])
                level, lr1 = lr0[:h, :w], lr1[:h, :w]
                lr, ipr = lr1 - level, (ipr1 - ipr0)[:h, :w]
                if cached:
                    args.cache.mkdir(parents=True, exist_ok=True)
                    np.savez_compressed(cached, level=level, lr=lr, ipr=ipr, lr1=lr1)
            entry = scenes.setdefault(r["scene"], {"level": level, "states": {}})
            entry["states"][int(r["value"])] = (lr, ipr, (level > -11) & (lr1 < 0))
            print(f"{r['scene']} {r['value']:+.0f}", flush=True)
    finally:
        if server is not None:
            server.close()
    return scenes


def mask(level, encoding, radius):
    plane = np.exp2(level) ** (1 / 2.2) if encoding == "gamma" else level
    return cv2.GaussianBlur(plane.astype(np.float32), (0, 0), radius / REDUCTION, borderType=cv2.BORDER_REFLECT)


def design(level, m, valid):
    return np.stack([level[valid], level[valid] * m[valid]], axis=1).astype(np.float64)


def study(scenes, value, encoding, radius):
    parts = {}
    for name, entry in scenes.items():
        if value not in entry["states"]:
            continue
        lr, ipr, valid = entry["states"][value]
        x = design(entry["level"], mask(entry["level"], encoding, radius), valid)
        residual = (lr - ipr)[valid].astype(np.float64)
        parts[name] = (x, residual, lr[valid].astype(np.float64))
    before = after = total = 0.0
    by_scene = {}
    for test in parts:
        xtx = sum(parts[s][0].T @ parts[s][0] / len(parts[s][1]) for s in parts if s != test)
        xty = sum(parts[s][0].T @ parts[s][1] / len(parts[s][1]) for s in parts if s != test)
        coef = np.linalg.solve(xtx, xty)
        x, residual, lr = parts[test]
        lr_var = float(np.sum((lr - lr.mean()) ** 2))
        b = float(np.sum((residual - residual.mean()) ** 2))
        left = residual - x @ coef
        a = float(np.sum((left - left.mean()) ** 2))
        by_scene[test] = {"before": b / lr_var, "after": a / lr_var,
                          "mean_residual_before": float(residual.mean()), "mean_residual_after": float(left.mean())}
        before, after, total = before + b, after + a, total + lr_var
    xtx = sum(p[0].T @ p[0] / len(p[1]) for p in parts.values())
    xty = sum(p[0].T @ p[1] / len(p[1]) for p in parts.values())
    alpha, beta = np.linalg.solve(xtx, xty)
    return {"before": before / total, "after": after / total, "alpha": float(alpha), "beta": float(beta),
            "by_scene": by_scene}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--cache", type=Path, help="directory for the reduced renders, outside the repository")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path)
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-clarte-masquage-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    scenes = load(args)
    values = sorted({v for e in scenes.values() for v in e["states"]})
    results = {}
    for value in values:
        for encoding in ENCODINGS:
            for radius in RADII:
                r = study(scenes, value, encoding, radius)
                results.setdefault(str(value), {})[f"{encoding}_{radius}"] = r
                print(f"clarity {value:+d} {encoding:5s} {radius:3d} px: unexplained {r['before']:.2f} -> {r['after']:.2f}"
                      f" (alpha {r['alpha']:+.3f}, beta {r['beta']:+.3f})", flush=True)
    record = {"campaign_id": campaign, "runner": "tests/study_clarity_masking.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "residual = L (alpha + beta m), L neutral log2 luminance, m Gaussian mask of the luminance "
                       "(gamma 2.2 or log2); first order of Moroney's y^(2^(k (c - m)))",
              "score": "share of the variance of Lightroom's change of log2 luminance left unexplained, "
                       "leave-one-scene-out, before (IPR band model) and after the masking term",
              "reduction": REDUCTION, "radii_native_px": list(RADII), "scenes": sorted(scenes),
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "results": results}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
