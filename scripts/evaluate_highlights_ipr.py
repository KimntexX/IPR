"""IPR against Lightroom for Highlights on every scene of tests/image/base.

For each scene with Lightroom Highlights references and each slider value,
IPR renders the state in memory (iprRenderServer, no TIFF) and the user's
perceptual criterion is applied: |delta L*| between the mean CIE L* of the
10 % brightest pixels of each engine's own neutral, in the variant, IPR minus
LR (block means 8x8 of linear luminance on both sides; no cross-engine
registration). The same gap on the neutrals is recorded, and the effect gap
(variant gap minus neutral gap) isolates the Highlights operator from the
development. Several render servers (e.g. before/after a change) are compared
in one pass over the Lightroom references.

Pass: |delta L*| <= 2 on 80 % of the scenes and at most two above 4.
Writes one immutable JSON. Usage:
  python scripts/evaluate_highlights_ipr.py --server NAME=PATH [--server NAME=PATH ...]
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import platform
import sys

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "scripts"))
from analysis import inputs, render  # noqa: E402
from analysis.native import linearize, sha256, write_immutable  # noqa: E402
from analyse_local_tone import BASE, FACTOR, block_mean  # noqa: E402
from fit_highlights_adaptive import DELTA_L, DELTA_L_MAX, PASS_OUTLIERS, PASS_SHARE, HIGHLIGHT_Y, HIGHLIGHT_SHARE, lightness  # noqa: E402

LUMA = np.array([.2126, .7152, .0722], dtype=np.float32)


def rendered_luminance(pixels):
    """Block-mean linear luminance of a uint16 sRGB render, by row bands."""
    height, width = (pixels.shape[0] // FACTOR) * FACTOR, (pixels.shape[1] // FACTOR) * FACTOR
    rows = []
    for top in range(0, height, FACTOR * 64):
        band = linearize(np.asarray(pixels[top:min(top + FACTOR * 64, height), :width], dtype=np.float32) / 65535) @ LUMA
        rows.append(band.reshape(-1, FACTOR, width // FACTOR, FACTOR).mean(axis=(1, 3)))
    return np.concatenate(rows).astype(np.float32)


def summary(errors):
    e = np.abs(np.array(list(errors.values())))
    within, outliers = int((e <= DELTA_L).sum()), int((e > DELTA_L_MAX).sum())
    return {"within": within, "outliers": outliers, "count": int(e.size), "mean_abs_delta_L": float(e.mean()),
            "passes": bool(within >= PASS_SHARE * e.size and outliers <= PASS_OUTLIERS)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", action="append", required=True, help="NAME=PATH of an iprRenderServer")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    servers = dict(item.split("=", 1) for item in args.server)
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == "highlights" and r.get("reference")]
    scenes = sorted({r["scene"] for r in records})
    values = sorted({r["value"] for r in records})
    sources = {p.stem: p for p in BASE.iterdir() if p.is_file() and p.suffix.lower() in (".cr3", ".cr2")}
    lr, weights = {}, {}
    for scene in scenes:
        mine = [r for r in records if r["scene"] == scene]
        y0, _ = block_mean(mine[0]["reference"], True)
        top = y0 >= np.quantile(y0, .9)
        weights[scene] = float(np.clip((y0 >= HIGHLIGHT_Y).mean() / HIGHLIGHT_SHARE, .1, 1))
        lr[scene] = {"neutral": float(lightness(y0[top]).mean())}
        for r in mine:
            y1, _ = block_mean(r["file"], True)
            lr[scene][r["value"]] = float(lightness(y1[top]).mean())
        print("LR", scene, flush=True)
    results, software = {}, {}
    for name, path in servers.items():
        per_value, neutral = {v: {} for v in values}, {}
        with render.RenderServer(path) as server:
            software[name] = {"server": str(server.executable), "server_sha256": server.sha256}
            for scene in scenes:
                pixels, _ = server.render(sources[scene], "neutral", 0)
                y0 = rendered_luminance(pixels)
                del pixels
                top = y0 >= np.quantile(y0, .9)
                neutral[scene] = float(lightness(y0[top]).mean()) - lr[scene]["neutral"]
                for value in values:
                    pixels, _ = server.render(sources[scene], "highlights", value)
                    y1 = rendered_luminance(pixels)
                    del pixels
                    per_value[value][scene] = float(lightness(y1[top]).mean()) - lr[scene][value]
                print(name, scene, " ".join(f"{per_value[v][scene]:+.1f}" for v in values), flush=True)
        bright = [s for s in scenes if weights[s] >= 1]
        results[name] = {"neutral_delta_L": neutral}
        for v in values:
            effect = {s: per_value[v][s] - neutral[s] for s in scenes}
            results[name][str(v)] = {"delta_L": per_value[v], "all": summary(per_value[v]),
                                     "highlight_scenes": summary({s: per_value[v][s] for s in bright}),
                                     "effect_delta_L": effect, "effect": summary(effect)}
    for value in values:
        print(f"HL {value:+.0f}: " + " | ".join(
            f"{n} all {r[str(value)]['all']['within']}/{r[str(value)]['all']['count']} >4:{r[str(value)]['all']['outliers']} "
            f"mean {r[str(value)]['all']['mean_abs_delta_L']:.1f}{' PASS' if r[str(value)]['all']['passes'] else ''}"
            f" ; HL scenes {r[str(value)]['highlight_scenes']['within']}/{r[str(value)]['highlight_scenes']['count']}"
            f" mean {r[str(value)]['highlight_scenes']['mean_abs_delta_L']:.1f}"
            f" ; effect {r[str(value)]['effect']['within']}/{r[str(value)]['effect']['count']}"
            f" mean {r[str(value)]['effect']['mean_abs_delta_L']:.1f}" for n, r in results.items()), flush=True)
    now = datetime.now(timezone.utc)
    result = {"campaign_id": f"campagne-hl-ipr-v002-{now.strftime('%Y%m%dT%H%M%S%fZ')}", "schema_version": 1,
              "measured_at_utc": now.isoformat(), "group": "lumiere", "feature": "highlights",
              "protocol": "docs/validation/protocols/protocole-lumieres.md",
              "method": {"ipr": "in-memory render of iprRenderServer (TIFF export pixels), no file written",
                         "lr": "Lightroom TIFF references of tests/image/base (IPR Lumieres complet presets)",
                         "zone": "10 % brightest pixels of each engine's own neutral, block mean 8x8",
                         "criterion": "mean CIE L* of the zone in the variant, IPR minus LR",
                         "effect": "variant delta L* minus the neutral delta L* of the same scene",
                         "pass": {"delta_L": DELTA_L, "max_delta_L": DELTA_L_MAX, "share_within": PASS_SHARE,
                                  "outliers_allowed": PASS_OUTLIERS},
                         "highlight_scenes": f"share of LR neutral pixels with Y >= {HIGHLIGHT_Y} at least {HIGHLIGHT_SHARE}"},
              "software": {"python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                           "script_sha256": sha256(Path(__file__)), "servers": software},
              "scenes": scenes, "values": values, "scene_weights": weights, "lr_lightness": lr, "results": results}
    output = args.output_dir / (result["campaign_id"] + ".json")
    write_immutable(output, result)
    print(output)


if __name__ == "__main__":
    main()
