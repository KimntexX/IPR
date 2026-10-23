"""Colour mixer slider tracks from the Lightroom mixer model (evidence JSON of
scripts/build_mixer_model.py): for each band, the colour at its response peak
and the colour each component reaches at -100 and +100, as QML arrays for
src/qml/ColorMixerModule.qml.

The reference colour sits at the band peak (Oklab lightness 0.68, chroma 0.14,
mid saturation and lightness cells); the model's rotation, chroma gain and
lightness gain at +-100 are applied, then the chroma is reduced until the
colour fits sRGB (channels are never clipped, so the hue stays exact).

Usage: python scripts/mixer_track_colours.py [MODEL_JSON]   (default: latest modele-melangeur-lr)
"""
import json
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import build_mixer_model as mm  # noqa: E402

M1 = np.array([[.4122214708, .5363325363, .0514459929], [.2119034982, .6806995451, .1073969566],
               [.0883024619, .2817188376, .6299787005]])
M2 = np.array([[.2104542553, .7936177850, -.0040720468], [1.9779984951, -2.4285922050, .4505937099],
               [.0259040371, .7827717662, -.8086757660]])
L0, C0, S_REF = 0.68, 0.14, 0.4


def srgb_hex(lightness, chroma, hue):
    """Encoded sRGB hex of an Oklab colour, chroma reduced until it fits the gamut."""
    lightness = float(np.clip(lightness, 0.05, 0.97))
    for scale in np.linspace(1, 0, 101):
        lab = np.array([lightness, chroma * scale * np.cos(np.radians(hue)), chroma * scale * np.sin(np.radians(hue))])
        rgb = np.linalg.inv(M1) @ (np.linalg.inv(M2) @ lab) ** 3
        if (rgb >= -1e-6).all() and (rgb <= 1 + 1e-6).all():
            break
    rgb = np.clip(rgb, 0, 1)
    encoded = np.where(rgb <= 0.0031308, 12.92 * rgb, 1.055 * rgb ** (1 / 2.4) - 0.055)
    return "#" + "".join(f"{int(round(255 * v)):02x}" for v in encoded)


def palette(model):
    sb = int(np.digitize(S_REF, mm.S_EDGES) - 1)
    lb = int(np.digitize(L0, mm.L_EDGES) - 1)
    lo, hi = mm.VALUES.index(-100), mm.VALUES.index(100)
    out = {key: [] for key in ("centre", "hueLow", "hueHigh", "saturationLow", "saturationHigh",
                               "luminanceLow", "luminanceHigh")}
    for band in mm.BANDS:
        blk = np.array(model["tables"]["per_band"][band])  # [component, value, quantity, bin, s, l]
        peak = int(np.argmax(np.abs(blk[0, hi, 0, :, sb, lb])))
        hue = ((model["windows"][band]["first_bin"] + peak) % mm.HUE_BINS + 0.5) * mm.HUE_STEP

        def reached(component, value):
            rotation, log_chroma, log_lightness = blk[component, value, :, peak, sb, lb]
            return srgb_hex(L0 * np.exp(log_lightness), C0 * np.exp(log_chroma), hue + rotation)
        out["centre"].append(srgb_hex(L0, C0, hue))
        out["hueLow"].append(reached(0, lo))
        out["hueHigh"].append(reached(0, hi))
        out["saturationLow"].append(reached(1, lo))
        out["saturationHigh"].append(reached(1, hi))
        out["luminanceLow"].append(reached(2, lo))
        out["luminanceHigh"].append(reached(2, hi))
    return out


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else sorted((ROOT / "docs/validation/raw").glob("modele-melangeur-lr-v*.json"))[-1]
    model = json.loads(path.read_text(encoding="utf-8"))
    print(f"// {path.name}")
    for key, values in palette(model).items():
        print(f'    readonly property var {key}Colors: [{", ".join(chr(34) + v + chr(34) for v in values)}]')


if __name__ == "__main__":
    main()
