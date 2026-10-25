"""Grain, Lightroom against IPR: amplitude by tone, size and colour of the grain.

For each grain state, the change of the display-encoded values against the
neutral of the same engine (Lightroom TIFF pair; IPR rendered in memory by
iprRenderServer) is described: mean and standard deviation of the luminance
change per encoded tone of the neutral, spatial autocorrelation at 1, 2, 4 and
8 pixels on a central crop (fineness of the grain), and the deviation of the
chroma change (monochrome grain gives none). Writes one immutable, versioned JSON.

Usage: python tests/study_grain.py --data-version N
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
from analysis.render import RenderServer, find_server  # noqa: E402
from study_lr_neutral_noise import read_tiff  # noqa: E402
from study_sources import git, sources  # noqa: E402

WEIGHTS = np.array([.2126, .7152, .0722])
TONES = (0, .05, .15, .3, .5, .7, .9, 1.01)   # encoded luminance of the neutral
LAGS = (1, 2, 4, 8)
CROP = 512


def describe(neutral, state):
    n = neutral.astype(np.float64) / 65535
    change = state.astype(np.float64) / 65535 - n
    luma, tone = change @ WEIGHTS, n @ WEIGHTS
    by_tone = []
    for lo, hi in zip(TONES, TONES[1:]):
        m = (tone >= lo) & (tone < hi)
        by_tone.append({"tone": [lo, min(hi, 1.0)], "pixels": int(m.sum()),
                        "mean": float(luma[m].mean()) if m.sum() > 1000 else None,
                        "deviation": float(luma[m].std()) if m.sum() > 1000 else None})
    cy, cx = luma.shape[0] // 2, luma.shape[1] // 2
    crop = luma[cy - CROP // 2:cy + CROP // 2, cx - CROP // 2:cx + CROP // 2]
    crop = crop - crop.mean()
    spectrum = np.abs(np.fft.fft2(crop)) ** 2
    autocorrelation = np.real(np.fft.ifft2(spectrum))
    autocorrelation /= autocorrelation[0, 0]
    return {"deviation": float(luma.std()), "chroma_deviation": float((change - luma[..., None]).std()),
            "autocorrelation": {str(k): float((autocorrelation[0, k] + autocorrelation[k, 0]) / 2) for k in LAGS},
            "by_tone": by_tone}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-version", type=int, required=True)
    parser.add_argument("--base", type=Path, default=ROOT / "tests/image/base")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    parser.add_argument("--server", type=Path)
    args = parser.parse_args(argv)
    stamp = datetime.now(timezone.utc)
    campaign = f"etude-grain-v{args.data_version:03d}-{stamp.strftime('%Y%m%dT%H%M%SZ')}"
    output = args.output_dir / f"{campaign}.json"
    if output.exists():
        raise SystemExit("Exists, nothing overwritten: " + str(output))
    records = [r for r in inputs.base_records(args.base, ["effets"]) if r["feature"] == "grain" and r.get("reference")]
    raws = {p.stem: p for p in sources(args.base, [])}
    start = time.monotonic()
    states = []
    with RenderServer(args.server or find_server(), timeout=300) as server:
        neutrals = {}
        for r in sorted(records, key=lambda r: (r["scene"], r["value"])):
            raw = raws.get(r["scene"])
            if raw is None:
                continue
            if raw not in neutrals:
                neutrals = {raw: server.render(raw, "neutral", 0)[0]}
            lr = describe(read_tiff(r["reference"]), read_tiff(r["file"]))
            ipr = describe(neutrals[raw], server.render(raw, "grain", int(r["value"]))[0])
            states.append({"scene": r["scene"], "value": r["value"], "lr_state": Path(r["file"]).name,
                           "lr_neutral": Path(r["reference"]).name, "lr": lr, "ipr": ipr})
            print(f"{r['scene']} {r['value']:+.0f}: deviation LR {lr['deviation']:.4f} IPR {ipr['deviation']:.4f}, "
                  f"correlation at 1 px LR {lr['autocorrelation']['1']:.2f} IPR {ipr['autocorrelation']['1']:.2f}, "
                  f"at 2 px LR {lr['autocorrelation']['2']:.2f} IPR {ipr['autocorrelation']['2']:.2f}, "
                  f"chroma LR {lr['chroma_deviation']:.4f} IPR {ipr['chroma_deviation']:.4f}", flush=True)
        executable, digest = str(server.executable), server.sha256
    record = {"campaign_id": campaign, "runner": "tests/study_grain.py",
              "commit": git("rev-parse", "HEAD"), "local_changes": git("status", "--short").splitlines(),
              "render_server": {"path": executable, "sha256": digest},
              "method": {"domain": "16-bit display-encoded values, change against the neutral of the same engine",
                         "tones": list(TONES), "autocorrelation_crop_px": CROP, "lags_px": list(LAGS)},
              "duration_seconds": round(time.monotonic() - start, 1), "python": platform.python_version(),
              "states": states}
    output.write_text(json.dumps(record, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"{output}\n{record['duration_seconds']:.0f} s")


if __name__ == "__main__":
    main()
