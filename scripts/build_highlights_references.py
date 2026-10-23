"""Generate src/core/ToneRecoveryHighlights.h: Highlights reference scenes.

Each scene of tests/image/base with Lightroom Highlights references gives its own
curve per slider value: median EV change of Lightroom (LR neutral -> variant,
pixel-aligned, block mean 8x8) per 0.25 EV bin of the neutral luminance, empty
bins taking the nearest measured one. IPR picks at run time the scenes whose
luminance statistics are closest to the image's and averages their curves
(fit-hl-adaptatif-v003: best transferable form on 49 scenes).

The statistics of each scene are those IPR measures when it opens an image
(ToneRecovery::measure order: mean, quantiles 1, 5, 10, 90, 95, 99.9 % of log2
luminance, one pixel in `stride`), here on its neutral render by
iprRenderServer. Brightening curves are stored before the luminance shoulder of
ToneRecovery::transfer (knee 0.80), which then restores the measured output.

Writes the header and one immutable JSON of evidence.
Usage: python scripts/build_highlights_references.py [--header ...] [--output-dir docs/validation/raw]
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
from analyse_local_tone import BASE, EDGES, FLOOR, block_mean, medians  # noqa: E402
from fit_highlights_adaptive import fill  # noqa: E402

LUMA = np.array([.2126, .7152, .0722])
LEVELS = (25, 50, 75, 100)
NEIGHBOURS = 5
KNEE, CAP = .80, .9999
BINS = len(EDGES) - 1
CENTRES = (EDGES[:-1] + EDGES[1:]) / 2


def ipr_statistics(pixels):
    """ToneRecovery::measure on the neutral render: stride to about 1e6 pixels, floor 2^-20."""
    stride = max(1, int(np.sqrt(pixels.shape[0] * pixels.shape[1] / 1e6)))
    rgb = linearize(np.asarray(pixels[::stride, ::stride, :3], dtype=np.float64) / 65535)
    v = np.sort(np.log2(np.maximum(rgb @ LUMA, 2.0 ** -20)).ravel())
    pick = lambda q: float(v[min(v.size - 1, int(q * (v.size - 1) + .5))])
    return [float(v.mean()), pick(.01), pick(.05), pick(.10), pick(.90), pick(.95), pick(.999)]


def before_shoulder(lift):
    """Lift whose shoulder (ToneRecovery::shoulder, knee 0.80, cap 0.9999) gives the measured output."""
    y = 2.0 ** CENTRES
    target = np.minimum(y * 2.0 ** lift, CAP)
    base = np.maximum(y, KNEE)
    soft = (target > y) & (target > KNEE)
    ratio = np.clip((target - base) / np.maximum(1 - base, 1e-6), 0, 1 - 1e-6)
    out = np.where(soft, base + (1 - base) * np.arctanh(ratio), target)
    return np.where(soft, np.log2(out / y), lift)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path, default=ROOT / "src/core/ToneRecoveryHighlights.h")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "docs/validation/raw")
    args = parser.parse_args()
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == "highlights" and r.get("reference")]
    scenes = sorted({r["scene"] for r in records})
    sources = {p.stem: p for p in BASE.iterdir() if p.is_file() and p.suffix.lower() in (".cr3", ".cr2")}
    statistics, curves, provenance = {}, {}, {}
    with render.RenderServer() as server:
        for scene in scenes:
            pixels, metadata = server.render(sources[scene], "neutral", 0)
            statistics[scene] = ipr_statistics(pixels)
            provenance[scene] = {"source": sources[scene].name, "source_sha256": metadata["declaration"]["source_sha256"],
                                 "dcp_profile": metadata["declaration"]["dcp_profile"], "references": {}}
            del pixels
        executable = {"server": str(server.executable), "server_sha256": server.sha256}
    for scene in scenes:
        mine = [r for r in records if r["scene"] == scene]
        values = {r["value"] for r in mine}
        if values != {sign * level for sign in (-1, 1) for level in LEVELS}:
            raise ValueError(f"{scene}: Highlights values {sorted(values)}")
        y0, meta = block_mean(mine[0]["reference"], True)
        index = np.clip(np.digitize(np.log2(np.maximum(y0, FLOOR)), EDGES) - 1, 0, BINS - 1)
        valid = y0 > FLOOR
        provenance[scene]["neutral"] = {"file": str(Path(mine[0]["reference"]).relative_to(BASE)), "sha256": meta["sha256"]}
        table = np.zeros((2, len(LEVELS), BINS))
        for r in mine:
            y1, meta1 = block_mean(r["file"], True)
            e = np.log2(np.maximum(y1, FLOOR / 4) / np.maximum(y0, FLOOR))
            curve = fill(medians(index[valid], e[valid], BINS))
            sign = 1 if r["value"] > 0 else 0
            table[sign, LEVELS.index(abs(int(r["value"])))] = before_shoulder(curve) if sign else curve
            provenance[scene]["references"][str(r["value"])] = {"file": str(Path(r["file"]).relative_to(BASE)),
                                                                "sha256": meta1["sha256"]}
        curves[scene] = table
        print("curves", scene, flush=True)

    X = np.array([statistics[s] for s in scenes])
    mean, scale = X.mean(0), X.std(0) + 1e-9
    f = lambda v: f"{v:.4f}f"
    lines = ["#ifndef IPR_CORE_TONERECOVERYHIGHLIGHTS_H", "#define IPR_CORE_TONERECOVERYHIGHLIGHTS_H", "",
             "// Generated by scripts/build_highlights_references.py from the Lightroom",
             "// Highlights references of tests/image/base (one scene per entry), see the",
             "// Lumières report. Do not edit by hand: regenerate.", "",
             "namespace ToneRecovery::Highlights {",
             f"inline constexpr float FirstBinEv = {f(EDGES[0])}; // bin i: [FirstBinEv + i BinEv, + BinEv)",
             f"inline constexpr float BinEv = {f(EDGES[1] - EDGES[0])};", f"inline constexpr int BinCount = {BINS};",
             f"inline constexpr int Neighbours = {NEIGHBOURS};",
             "// Statistics in ToneRecovery::Model::Statistic order, standardised over the scenes.",
             f"inline constexpr int StatisticCount = {len(mean)};",
             "inline constexpr float StatisticMean[StatisticCount] = {" + ", ".join(map(f, mean)) + "};",
             "inline constexpr float StatisticScale[StatisticCount] = {" + ", ".join(map(f, scale)) + "};",
             "struct Scene {", "    const char *name;", "    float position[StatisticCount]; // standardised statistics",
             "    float lift[2][4][BinCount]; // EV lift per sign (-, +), slider level 25..100 and bin", "};",
             f"inline constexpr int SceneCount = {len(scenes)};", "inline constexpr Scene Scenes[SceneCount] = {"]
    for scene in scenes:
        position = (np.array(statistics[scene]) - mean) / scale
        lines.append(f'    {{"{scene}", {{{", ".join(map(f, position))}}}, {{')
        for sign in range(2):
            lines.append("        {")
            lines += ["            {" + ", ".join(map(f, curves[scene][sign, level])) + "}," for level in range(len(LEVELS))]
            lines.append("        },")
        lines.append("    }},")
    lines += ["};", "}", "", "#endif // IPR_CORE_TONERECOVERYHIGHLIGHTS_H", ""]
    args.header.write_text("\n".join(lines), encoding="utf-8", newline="\n")

    now = datetime.now(timezone.utc)
    result = {"campaign_id": f"references-hl-v001-{now.strftime('%Y%m%dT%H%M%S%fZ')}", "schema_version": 1,
              "measured_at_utc": now.isoformat(), "group": "lumiere", "feature": "highlights",
              "purpose": "Reference scenes of the production Highlights model (ToneRecoveryHighlights.h)",
              "method": {"curve": "median log2(Y1/Y0) per 0.25 EV bin of the LR neutral luminance, block mean 8x8, "
                                  "empty bins from the nearest measured bin",
                         "brightening": f"stored before the luminance shoulder (knee {KNEE}, cap {CAP})",
                         "statistics": "ToneRecovery::measure order on the IPR neutral render (iprRenderServer)",
                         "neighbours": NEIGHBOURS, "levels": list(LEVELS),
                         "selection_evidence": "fit-hl-adaptatif-v003-20261020T130452564065Z"},
              "software": {"python": sys.version, "numpy": np.__version__, "platform": platform.platform(),
                           "script_sha256": sha256(Path(__file__)), **executable},
              "header": str(args.header.relative_to(ROOT)), "header_sha256": sha256(args.header),
              "scenes": scenes, "statistics": statistics,
              "standardisation": {"mean": mean.tolist(), "scale": scale.tolist()},
              "curves": {s: curves[s].tolist() for s in scenes}, "provenance": provenance}
    output = args.output_dir / (result["campaign_id"] + ".json")
    write_immutable(output, result)
    print("written:", args.header, output)


if __name__ == "__main__":
    main()
