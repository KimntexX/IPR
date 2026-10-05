#!/usr/bin/env python3
"""Compare saturation variants against neutral Lightroom and IPR TIFFs."""

from contextlib import redirect_stdout
from pathlib import Path
import sys

import numpy as np
from PIL import Image


ROOT = Path("tests/image/base")
NEUTRAL_ROOT = ROOT / "neutre"
SATURATION_ROOT = ROOT / "saturation"
IMAGE_IDS = (
    "6V1A0007", "6V1A0043", "6V1A0767", "6V1A9073",
    "6V1A9119", "6V1A9734", "6V1A9944", "6V1A9947",
)
SAMPLE_SIZE = (580, 870)


def load_linear_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        image = image.convert("RGB").resize(SAMPLE_SIZE, Image.Resampling.BOX)
        encoded = np.asarray(image, dtype=np.float32) / 255.0
    return np.where(encoded <= 0.04045, encoded / 12.92,
                    ((encoded + 0.055) / 1.055) ** 2.4)


def paths(image_id: str, engine: str):
    suffix = ".tif" if engine == "LR" else "-IPR.tiff"
    neutral_suffix = "-3.tif" if engine == "LR" else "-IPR-3.tiff"
    variant_prefix = "" if engine == "LR" else "-IPR"
    return (NEUTRAL_ROOT / f"{image_id}{neutral_suffix}",
            SATURATION_ROOT / f"{image_id}{variant_prefix}.tif" if engine == "LR"
            else SATURATION_ROOT / f"{image_id}{variant_prefix}.tiff",
            SATURATION_ROOT / f"{image_id}-2.tif" if engine == "LR"
            else SATURATION_ROOT / f"{image_id}{variant_prefix}-2.tiff")


def relative_chroma(rgb: np.ndarray) -> np.ndarray:
    maximum = rgb.max(axis=2)
    minimum = rgb.min(axis=2)
    return (maximum - minimum) / np.maximum(maximum, 1e-5)


def measures(reference: np.ndarray, variant: np.ndarray):
    reference_chroma = relative_chroma(reference)
    variant_chroma = relative_chroma(variant)
    luminance_weights = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)
    reference_luminance = reference @ luminance_weights
    variant_luminance = variant @ luminance_weights
    valid = reference_luminance > 0.01
    low_chroma = valid & (reference_chroma < 0.25) & (reference_chroma >= 0.02)
    high_chroma = valid & (reference_chroma >= 0.50)

    def relative_delta(mask: np.ndarray) -> float:
        return float(((variant_chroma[mask] - reference_chroma[mask]) /
                      reference_chroma[mask]).mean()) if mask.any() else 0.0

    return (
        float((variant_chroma[valid] - reference_chroma[valid]).mean()),
        relative_delta(low_chroma),
        relative_delta(high_chroma),
        float(variant_luminance[valid].mean() / reference_luminance[valid].mean()),
        float((variant >= 0.999).any(axis=2).mean()),
    )


def analyse(engine: str):
    aggregates = {"bare": [], "suffix_2": []}
    missing = []
    for image_id in IMAGE_IDS:
        reference_path, bare_path, suffix_2_path = paths(image_id, engine)
        if not all(path.exists() for path in
                   (reference_path, bare_path, suffix_2_path)):
            missing.append(image_id)
            continue
        reference = load_linear_rgb(reference_path)
        aggregates["bare"].append(measures(reference, load_linear_rgb(bare_path)))
        aggregates["suffix_2"].append(
            measures(reference, load_linear_rgb(suffix_2_path)))

    print(f"  {engine}: {len(IMAGE_IDS) - len(missing)} scenes")
    if missing:
        print("    Excluded: " + ", ".join(missing))
    for label, entries in aggregates.items():
        if not entries:
            continue
        values = np.mean(entries, axis=0)
        print(f"    {label}: delta_chroma={values[0]:.4f}; "
              f"relative_low={values[1]:.4f}; relative_high={values[2]:.4f}; "
              f"Y={values[3]:.4f}; clip={100.0 * values[4]:.3f}%")


if __name__ == "__main__":
    arguments = list(sys.argv[1:])
    output_path = None
    if "--output" in arguments:
        index = arguments.index("--output")
        if index + 1 >= len(arguments):
            raise SystemExit("--output expects a path")
        output_path = Path(arguments[index + 1])
        del arguments[index:index + 2]
    if arguments:
        raise SystemExit("Unknown arguments: " + ", ".join(arguments))
    output = output_path.open("w", encoding="utf-8") if output_path else sys.stdout
    try:
        with redirect_stdout(output):
            print("SATURATION")
            for engine_name in ("LR", "IPR"):
                analyse(engine_name)
    finally:
        if output_path:
            output.close()
