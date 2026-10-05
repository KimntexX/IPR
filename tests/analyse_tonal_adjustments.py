#!/usr/bin/env python3
"""Measure Lightroom/IPR black and shadow adjustment TIFF triplets.

Run from the repository root with a Python environment providing Pillow and
NumPy.  The analyser samples each TIFF uniformly, linearises sRGB, and reports
the response relative to each engine's own EV0 render.  This deliberately
avoids treating baseline rendering differences as slider differences.
"""

from pathlib import Path
import sys
from contextlib import redirect_stdout

import numpy as np
from PIL import Image


ROOT = Path("tests/image/base")
NEUTRAL_ROOT = ROOT / "neutre"
IMAGE_IDS = (
    "6V1A0007", "6V1A0043", "6V1A0767", "6V1A9073",
    "6V1A9119", "6V1A9734", "6V1A9944", "6V1A9947",
)
SAMPLE_SIZE = (580, 870)


def load_luminance(path: Path) -> np.ndarray:
    """Return a uniform, linear-sRGB luminance sample for a TIFF."""
    with Image.open(path) as image:
        image = image.convert("RGB").resize(SAMPLE_SIZE, Image.Resampling.BOX)
        rgb = np.asarray(image, dtype=np.float32) / 255.0
    linear = np.where(rgb <= 0.04045, rgb / 12.92,
                      ((rgb + 0.055) / 1.055) ** 2.4)
    return (0.2126 * linear[..., 0] + 0.7152 * linear[..., 1] +
            0.0722 * linear[..., 2])


def image_paths(image_id: str, adjustment_set: str, engine: str):
    baseline_root = (NEUTRAL_ROOT if adjustment_set in {"blanc", "hl", "contraste"}
                     else ROOT)
    if engine == "LR":
        return (baseline_root / f"{image_id}-3.tif",
                ROOT / adjustment_set / f"{image_id}.tif",
                ROOT / adjustment_set / f"{image_id}-2.tif")
    negative = ROOT / adjustment_set / f"{image_id}-IPR.tiff"
    # v3 changes only positive branches of Blancs/Hautes lumières.  Reuse the
    # archived v2 -100 output when it is deliberately not regenerated; this
    # keeps a complete triplet without treating a missing file as zero.
    if adjustment_set in {"blanc", "hl"} and not negative.exists():
        archived = (ROOT / adjustment_set /
                    "archive-ipr-pre-correction-v2-2026-10-02" /
                    f"{image_id}-IPR.tiff")
        if archived.exists():
            negative = archived
    return (baseline_root / f"{image_id}-IPR-3.tiff", negative,
            ROOT / adjustment_set / f"{image_id}-IPR-2.tiff")


def analyse(adjustment_set: str, engine: str):
    values = {name: np.zeros(3, dtype=np.float64)
              for name in ("low", "mid", "high")}
    clipped = np.zeros(3, dtype=np.float64)
    pixel_count = 0
    included, missing = [], []
    for image_id in IMAGE_IDS:
        paths = image_paths(image_id, adjustment_set, engine)
        if not all(path.exists() for path in paths):
            missing.append(image_id)
            continue
        baseline, minus, plus = (load_luminance(path) for path in paths)
        q20, q40, q60, q80 = np.quantile(baseline, (0.20, 0.40, 0.60, 0.80))
        zones = {
            "low": baseline <= q20,
            "mid": (baseline >= q40) & (baseline <= q60),
            "high": baseline >= q80,
        }
        for zone, mask in zones.items():
            values[zone] += (baseline[mask].sum(), minus[mask].sum(),
                             plus[mask].sum())
        clipped += ((baseline <= 0.001).sum(), (minus <= 0.001).sum(),
                    (plus <= 0.001).sum())
        pixel_count += baseline.size
        included.append(image_id)

    print(f"  {engine}: {len(included)} scènes ({', '.join(included)})")
    if missing:
        print(f"    Exclues (triplet incomplet) : {', '.join(missing)}")
    if pixel_count == 0:
        print("    Aucun triplet complet à mesurer.")
        return
    for zone in ("low", "mid", "high"):
        baseline, minus, plus = values[zone]
        print(f"    {zone:4s} : ratio -100={minus / baseline:.4f}, "
              f"+100={plus / baseline:.4f}; "
              f"delta -100={(minus - baseline) / pixel_count:.5f}, "
              f"+100={(plus - baseline) / pixel_count:.5f}")
    print("    pixels Y <= 0.001 : " + ", ".join(
        f"{100.0 * item / pixel_count:.3f}%" for item in clipped))


if __name__ == "__main__":
    arguments = list(sys.argv[1:])
    output_path = None
    if "--output" in arguments:
        output_index = arguments.index("--output")
        if output_index + 1 >= len(arguments):
            raise SystemExit("--output attend un chemin de fichier")
        output_path = Path(arguments[output_index + 1])
        del arguments[output_index:output_index + 2]
    requested_sets = tuple(arguments) or ("noir", "ombre")
    invalid_sets = set(requested_sets) - {
        "noir", "ombre", "blanc", "hl", "contraste"}
    if invalid_sets:
        raise SystemExit("Réglage invalide : " + ", ".join(sorted(invalid_sets)))
    output = (output_path.open("w", encoding="utf-8")
              if output_path else sys.stdout)
    try:
        with redirect_stdout(output):
            for adjustment_set in requested_sets:
                print(f"SET {adjustment_set}")
                for engine in ("LR", "IPR"):
                    analyse(adjustment_set, engine)
    finally:
        if output_path:
            output.close()
