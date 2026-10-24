"""Amplitude-dependent band gains of Lightroom's texture and clarity.

Lightroom amplifies small variations of each band much more than large ones
(edges are preserved). On each Lightroom neutral -> state pair, for each
difference-of-Gaussian band of the log luminance (same bands as
study_effects_response.py), the model

    variant_band = d + k * d * w(|d|),   w(a) = 1 / (1 + (a / A)^2)

is fitted by least squares on all pairs of a slider value at once (k in
closed form for each A of a grid; A shared by the values of a sign). d is the
neutral's band in log2 units (EV). Writes one immutable, versioned JSON.

Usage: python tests/study_effects_amplitude.py --data-version N [--feature texture ...]
"""
import argparse
import json
import platform
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from analysis import inputs  # noqa: E402
from study_effects_response import BANDS, SIGMAS, gaussian, log_luma  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git  # noqa: E402

A_GRID = np.geomspace(0.02, 4.0, 40)   # EV
SAMPLES = 300_000   # random valid pixels per pair (fixed seed)


def pair_bands(neutral, variant):
    y0 = neutral @ np.array([.2126, .7152, .0722], np.float32)
    valid = (y0 > 0.002) & (neutral.max(axis=2) < 0.98) & (variant.max(axis=2) < 0.98)
    l0, l1 = log_luma(neutral), log_luma(variant)
    g0 = [l0] + [gaussian(l0, s) for s in SIGMAS]
    g1 = [l1] + [gaussian(l1, s) for s in SIGMAS]
    index = np.flatnonzero(valid)
    index = np.random.default_rng(0).choice(index, size=min(SAMPLES, index.size), replace=False)
    out = []
    for i in range(len(SIGMAS)):
        band0, band1 = (g0[i] - g0[i + 1]).ravel(), (g1[i] - g1[i + 1]).ravel()
        out.append((band0[index].astype(np.float32), band1[index].astype(np.float32)))
    return out


def fit(samples):
    """Best (A, k) per value for one band; A shared across the values given."""
    best = None
    for A in A_GRID:
        cost, ks = 0.0, {}
        for value, (d0, d1) in samples.items():
            x = d0 / (1 + (np.abs(d0) / A) ** 2)
            k = float(np.dot(x, d1 - d0) / max(np.dot(x, x), 1e-12))
            r = d1 - d0 - k * x
            cost += float(np.dot(r, r))
            ks[value] = k
        if best is None or cost < best[0]:
            best = (cost, float(A), ks)
    return best


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--feature", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    features = args.feature or ["texture", "clarity"]
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-amplitude-effets-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    records = [r for r in inputs.base_records(args.base, ["effets"]) if r["feature"] in features and r.get("reference")]
    result = {}
    for feature in features:
        per_value = {}   # value -> band index -> list of (d0, d1)
        scenes = sorted({r["scene"] for r in records if r["feature"] == feature})
        for r in records:
            if r["feature"] != feature:
                continue
            bands = pair_bands(linear(read_tiff(r["reference"])), linear(read_tiff(r["file"])))
            for b, pair in enumerate(bands):
                per_value.setdefault(int(r["value"]), {}).setdefault(b, []).append(pair)
            print(f"{feature} {r['scene']} {r['value']:+.0f}", flush=True)
        fitted = {}
        for sign in (-1, 1):
            values = sorted(v for v in per_value if v * sign > 0)
            for b in range(len(SIGMAS)):
                samples = {v: tuple(np.concatenate([p[i] for p in per_value[v][b]]) for i in range(2)) for v in values}
                cost, A, ks = fit(samples)
                for v in values:
                    fitted.setdefault(str(v), {})[BANDS[b]] = {"k": ks[v], "A_ev": A}
        result[feature] = {"scenes": scenes, "fit": fitted}
        for v in sorted(fitted, key=int):
            print(feature, v, " ".join(f"{fitted[v][b]['k']:+.2f}/{fitted[v][b]['A_ev']:.2f}" for b in BANDS[:-1]))
    record = {"campaign_id": campaign, "runner": "tests/study_effects_amplitude.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "model": "variant_band = d + k d / (1 + (|d|/A)^2), d the neutral band in log2 units; A shared per band "
                       "by the values of a sign; least squares on every pair of the value",
              "bands": list(BANDS[:-1]), "sigmas": list(SIGMAS), "samples_per_pair": SAMPLES,
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "features": result}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
