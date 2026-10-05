#!/usr/bin/env python3
"""Compare green-magenta tint variants against neutral Lightroom and IPR TIFFs."""

from contextlib import redirect_stdout
from pathlib import Path
import sys

import numpy as np
from PIL import Image


ROOT = Path("tests/image/base")
NEUTRAL_ROOT = ROOT / "neutre"
TINT_ROOT = ROOT / "nuance"
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
    if engine == "LR":
        return (NEUTRAL_ROOT / f"{image_id}-3.tif",
                TINT_ROOT / f"{image_id}.tif",
                TINT_ROOT / f"{image_id}-2.tif")
    return (NEUTRAL_ROOT / f"{image_id}-IPR-3.tiff",
            TINT_ROOT / f"{image_id}-IPR.tiff",
            TINT_ROOT / f"{image_id}-IPR-2.tiff")


def measures(reference: np.ndarray, variant: np.ndarray):
    epsilon = 1e-5
    reference_mean = reference.mean(axis=(0, 1))
    variant_mean = variant.mean(axis=(0, 1))
    rgb_ratio = variant_mean / np.maximum(reference_mean, epsilon)
    luminance_weights = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)
    luminance_ratio = (variant_mean @ luminance_weights) / max(
        float(reference_mean @ luminance_weights), epsilon)
    # Positive values move toward magenta (R+B); negative values toward green.
    reference_axis = np.log(np.sqrt(np.maximum(reference[..., 0], epsilon) *
                                    np.maximum(reference[..., 2], epsilon)) /
                            np.maximum(reference[..., 1], epsilon)).mean()
    variant_axis = np.log(np.sqrt(np.maximum(variant[..., 0], epsilon) *
                                  np.maximum(variant[..., 2], epsilon)) /
                          np.maximum(variant[..., 1], epsilon)).mean()
    clipped = (variant >= 0.999).any(axis=2).mean()
    return rgb_ratio, luminance_ratio, variant_axis - reference_axis, clipped


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
        rgb = np.mean([entry[0] for entry in entries], axis=0)
        luminance = float(np.mean([entry[1] for entry in entries]))
        axis = float(np.mean([entry[2] for entry in entries]))
        clipped = float(np.mean([entry[3] for entry in entries]))
        print(f"    {label}: RGB={rgb[0]:.4f},{rgb[1]:.4f},{rgb[2]:.4f}; "
              f"Y={luminance:.4f}; delta_log_RB_over_G={axis:.4f}; "
              f"clip={100.0 * clipped:.3f}%")


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
            print("TINT")
            for engine_name in ("LR", "IPR"):
                analyse(engine_name)
    finally:
        if output_path:
            output.close()
