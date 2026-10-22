"""Visual comparison sheets, IPR against Lightroom, for one Lumières control.

For each scene and value: the IPR state rendered in memory (iprRenderServer)
and the Lightroom TIFF reference, side by side at reduced size, the map of the
effect difference in CIE L* ((IPR variant - IPR neutral) - (LR variant - LR
neutral): what each engine's control does, without the development gap), and
the per-channel histograms of both engines in display (sRGB) codes, as the
IHM shows them. PNG sheets go to a build directory, never to tests/image.
Usage: python scripts/compare_lumieres_visual.py --feature highlights --scene 6V1A0783 [--scene ...]
"""
import argparse
from pathlib import Path
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "scripts"))
from analysis import inputs, render  # noqa: E402
from analysis.native import inspect, linearize  # noqa: E402

BASE = ROOT / "tests/image/base"
LUMA = np.array([.2126, .7152, .0722])
SIZE = 640


def reduce(pixels):
    """uint16 sRGB (H, W, >=3) -> encoded float (h, w, 3), block mean to about SIZE wide."""
    factor = max(1, pixels.shape[1] // SIZE)
    h, w = (pixels.shape[0] // factor) * factor, (pixels.shape[1] // factor) * factor
    rows = []
    for top in range(0, h, factor * 32):
        band = np.asarray(pixels[top:min(top + factor * 32, h), :w, :3], dtype=np.float32) / 65535
        rows.append(band.reshape(-1, factor, w // factor, factor, 3).mean(axis=(1, 3)))
    return np.concatenate(rows)


def lightness(encoded):
    y = linearize(np.clip(encoded, 0, 1)) @ LUMA
    return np.where(y > 216 / 24389, 116 * np.cbrt(y) - 16, y * 24389 / 27)


def histogram(ax, image, title):
    for c, colour in enumerate(("red", "green", "blue")):
        ax.hist(image[..., c].ravel(), bins=128, range=(0, 1), histtype="step", color=colour, density=True)
    ax.hist(lightness(image).ravel() / 100, bins=128, range=(0, 1), histtype="step", color="black", density=True)
    ax.set_title(title, fontsize=8)
    ax.set_yticks([])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--feature", default="highlights")
    parser.add_argument("--scene", action="append", required=True)
    parser.add_argument("--values", default="-100,100")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build-colorimetrie-inventory/visual")
    args = parser.parse_args()
    values = [float(v) for v in args.values.split(",")]
    records = [r for r in inputs.base_records(BASE, ["lumiere"]) if r["feature"] == args.feature and r.get("reference")]
    sources = {p.stem: p for p in BASE.iterdir() if p.is_file() and p.suffix.lower() in (".cr3", ".cr2")}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with render.RenderServer() as server:
        for scene in args.scene:
            mine = {r["value"]: r for r in records if r["scene"] == scene}
            lr = {"neutral": reduce(inspect(Path(next(iter(mine.values()))["reference"]))[0])}
            ipr = {"neutral": reduce(server.render(sources[scene], "neutral", 0)[0])}
            for value in values:
                lr[value] = reduce(inspect(Path(mine[value]["file"]))[0])
                ipr[value] = reduce(server.render(sources[scene], args.feature, value)[0])
            figure, axes = plt.subplots(len(values), 4, figsize=(17, 3.8 * len(values)), squeeze=False)
            for row, value in enumerate(values):
                a, b = ipr[value], lr[value]
                h, w = min(a.shape[0], b.shape[0]), min(a.shape[1], b.shape[1])
                effect = (lightness(a[:h, :w]) - lightness(ipr["neutral"][:h, :w])) - \
                         (lightness(b[:h, :w]) - lightness(lr["neutral"][:h, :w]))
                axes[row, 0].imshow(np.clip(a, 0, 1)); axes[row, 0].set_title(f"IPR {args.feature} {value:+.0f}", fontsize=8)
                axes[row, 1].imshow(np.clip(b, 0, 1)); axes[row, 1].set_title(f"Lightroom {args.feature} {value:+.0f}", fontsize=8)
                image = axes[row, 2].imshow(effect, cmap="RdBu_r", vmin=-15, vmax=15)
                axes[row, 2].set_title("effect IPR - LR, delta L* (red: IPR brighter)", fontsize=8)
                figure.colorbar(image, ax=axes[row, 2], fraction=.04)
                histogram(axes[row, 3], a, "")
                for c, colour in enumerate(("red", "green", "blue")):
                    axes[row, 3].hist(b[..., c].ravel(), bins=128, range=(0, 1), histtype="step", color=colour,
                                      linestyle=":", density=True)
                axes[row, 3].set_title("histograms: IPR solid, LR dotted (black: L*/100 IPR)", fontsize=8)
                for ax in axes[row, :3]:
                    ax.set_xticks([]); ax.set_yticks([])
            figure.tight_layout()
            output = args.output_dir / f"{scene}-{args.feature}.png"
            figure.savefig(output, dpi=80)
            plt.close(figure)
            print(output, flush=True)


if __name__ == "__main__":
    main()
