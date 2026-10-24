"""Tone protection of Lightroom's texture and clarity.

Lightroom darkens less near black and brightens less near white than a pure
band operator in log luminance would. The band model of LocalContrast (the
amplitude fit of study_effects_amplitude.py) is applied to each Lightroom
neutral; Lightroom's actual change of log luminance is then regressed on the
predicted change, separately for darkening and brightening, per neutral
luminance bin: the slope is the protection factor at that level. Writes one
immutable, versioned JSON.

Usage: python tests/study_effects_tone_protection.py --data-version N --amplitude-fit FIT.json [--feature clarity ...]
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
from analysis.native import sha256  # noqa: E402
from study_lr_neutral_noise import linear, read_tiff  # noqa: E402
from study_sources import git  # noqa: E402

WEIGHTS = np.array([.2126, .7152, .0722], np.float32)
LOG_FLOOR = 1e-3   # as LocalContrast
LEVELS = np.geomspace(0.002, 1.0, 13)
SAMPLES = 400_000


def blur_at_scale(plane, factor):
    """Same approximation as LocalContrast: sigma 2 * factor, at 1/factor resolution."""
    if factor <= 1:
        return cv2.GaussianBlur(plane, (0, 0), 2.0 * factor, borderType=cv2.BORDER_REFLECT)
    small = cv2.resize(plane, (max(1, plane.shape[1] // factor), max(1, plane.shape[0] // factor)),
                       interpolation=cv2.INTER_AREA)
    small = cv2.GaussianBlur(small, (0, 0), 2.0, borderType=cv2.BORDER_REFLECT)
    return cv2.resize(small, (plane.shape[1], plane.shape[0]), interpolation=cv2.INTER_LINEAR)


def predicted_delta(log_luminance, k, a):
    """Change of log2 luminance of the band model, k and A per band."""
    delta = np.zeros_like(log_luminance)
    previous = log_luminance
    current = cv2.GaussianBlur(log_luminance, (0, 0), 1.0, borderType=cv2.BORDER_REFLECT)
    for b in range(len(k)):
        if b > 0:
            current = blur_at_scale(log_luminance, 1 << (b - 1))
        d = previous - current
        delta += d * (k[b] / (1 + (d / a[b]) ** 2))
        previous = current
    return delta


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--amplitude-fit", type=Path, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--feature", action="append", default=[])
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args(argv)
    features = args.feature or ["texture", "clarity"]
    fit = json.loads(args.amplitude_fit.read_text(encoding="utf-8"))
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-protection-effets-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    start = time.monotonic()
    records = [r for r in inputs.base_records(args.base, ["effets"]) if r["feature"] in features and r.get("reference")]
    result = {}
    rng = np.random.default_rng(0)
    for feature in features:
        bands = fit["bands"]
        acc = {}   # (value, sign, level) -> [sum actual*pred, sum pred^2, n]
        for r in records:
            if r["feature"] != feature:
                continue
            value = str(int(r["value"]))
            params = fit["features"][feature]["fit"][value]
            k = [params[b]["k"] for b in bands]
            a = [params[b].get("A", params[b].get("A_ev")) for b in bands]   # v001 named it A_ev
            neutral, variant = linear(read_tiff(r["reference"])), linear(read_tiff(r["file"]))
            y0 = np.clip(neutral @ WEIGHTS, 0, None)
            valid = (y0 > 0.002) & (neutral.max(axis=2) < 0.98) & (variant.max(axis=2) < 0.98)
            l0 = np.log2(y0 + LOG_FLOOR).astype(np.float32)
            l1 = np.log2(np.clip(variant @ WEIGHTS, 0, None) + LOG_FLOOR).astype(np.float32)
            pred = predicted_delta(l0, k, a)
            index = np.flatnonzero(valid)
            index = rng.choice(index, size=min(SAMPLES, index.size), replace=False)
            actual, predicted, level = (l1 - l0).ravel()[index], pred.ravel()[index], y0.ravel()[index]
            for sign in (-1, 1):
                for i, (lo, hi) in enumerate(zip(LEVELS[:-1], LEVELS[1:])):
                    m = (np.sign(predicted) == sign) & (level >= lo) & (level < hi)
                    key = (value, sign, i)
                    s = acc.setdefault(key, [0.0, 0.0, 0])
                    s[0] += float(np.dot(actual[m], predicted[m]))
                    s[1] += float(np.dot(predicted[m], predicted[m]))
                    s[2] += int(m.sum())
            print(f"{feature} {r['scene']} {value}", flush=True)
        table = {}
        for (value, sign, i), (num, den, n) in sorted(acc.items()):
            if n < 5000:
                continue
            table.setdefault(value, {}).setdefault("darken" if sign < 0 else "brighten", []).append(
                {"level": float(np.sqrt(LEVELS[i] * LEVELS[i + 1])), "factor": num / den, "pixels": n})
        result[feature] = table
        for value in sorted(table, key=int):
            for side in ("darken", "brighten"):
                row = table[value].get(side, [])
                print(feature, value, side, " ".join(f"{e['level']:.3f}:{e['factor']:.2f}" for e in row))
    record = {"campaign_id": campaign, "runner": "tests/study_effects_tone_protection.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "amplitude_fit": {"file": args.amplitude_fit.name, "sha256": sha256(args.amplitude_fit)},
              "method": "slope of Lightroom's log2 luminance change on the band model's predicted change, "
                        "per sign of the prediction and neutral luminance bin " + str(LEVELS.tolist()),
              "samples_per_pair": SAMPLES, "duration_seconds": round(time.monotonic() - start, 1),
              "python": platform.python_version(), "features": result}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
