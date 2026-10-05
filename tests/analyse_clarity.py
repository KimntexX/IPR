#!/usr/bin/env python3
"""Compare clarity variants through medium-radius local-contrast metrics."""

from contextlib import redirect_stdout
from pathlib import Path
import sys

import numpy as np
from PIL import Image


ROOT = Path("tests/image/base")
NEUTRAL_ROOT = ROOT / "neutre"
CLARITY_ROOT = ROOT / "clarte"
IMAGE_IDS = ("6V1A0007", "6V1A0043", "6V1A0767", "6V1A9073",
             "6V1A9119", "6V1A9734", "6V1A9944", "6V1A9947")
PATCH_SIZE = 512
PATCH_POSITIONS = ((0.10, 0.18), (0.50, 0.18), (0.82, 0.18),
                   (0.10, 0.68), (0.50, 0.68), (0.82, 0.68))
LUMINANCE = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)


def load_linear_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        width, height = image.size
        patches = []
        for fraction_x, fraction_y in PATCH_POSITIONS:
            left = int((width - PATCH_SIZE) * fraction_x)
            top = int((height - PATCH_SIZE) * fraction_y)
            patch = image.crop((left, top, left + PATCH_SIZE, top + PATCH_SIZE))
            patches.append(np.asarray(patch.convert("RGB"), dtype=np.float32) / 255.0)
        encoded = np.concatenate(patches, axis=0)
    return np.where(encoded <= 0.04045, encoded / 12.92,
                    ((encoded + 0.055) / 1.055) ** 2.4)


def paths(image_id: str, engine: str):
    if engine == "LR":
        return (NEUTRAL_ROOT / f"{image_id}-3.tif", CLARITY_ROOT / f"{image_id}.tif",
                CLARITY_ROOT / f"{image_id}-2.tif")
    return (NEUTRAL_ROOT / f"{image_id}-IPR-3.tiff",
            CLARITY_ROOT / f"{image_id}-IPR.tiff",
            CLARITY_ROOT / f"{image_id}-IPR-2.tiff")


def box_blur(image: np.ndarray, radius: int = 8) -> np.ndarray:
    padded = np.pad(image, radius, mode="edge")
    integral = np.pad(padded, ((1, 0), (1, 0)), mode="constant").cumsum(0).cumsum(1)
    size = 2 * radius + 1
    return (integral[size:, size:] - integral[:-size, size:] -
            integral[size:, :-size] + integral[:-size, :-size]) / float(size * size)


def chroma(rgb: np.ndarray) -> np.ndarray:
    return (rgb.max(axis=2) - rgb.min(axis=2)) / np.maximum(rgb.max(axis=2), 1e-5)


def measures(reference: np.ndarray, variant: np.ndarray):
    reference_luma, variant_luma = reference @ LUMINANCE, variant @ LUMINANCE
    reference_residual = float(np.abs(reference_luma - box_blur(reference_luma)).mean())
    variant_residual = float(np.abs(variant_luma - box_blur(variant_luma)).mean())
    reference_gradient = float((np.abs(np.diff(reference_luma, axis=0)).mean() +
                                np.abs(np.diff(reference_luma, axis=1)).mean()) / 2.0)
    variant_gradient = float((np.abs(np.diff(variant_luma, axis=0)).mean() +
                              np.abs(np.diff(variant_luma, axis=1)).mean()) / 2.0)
    valid = reference_luma > 0.01
    return (variant_residual / max(reference_residual, 1e-8),
            variant_gradient / max(reference_gradient, 1e-8),
            float(variant_luma[valid].mean() / reference_luma[valid].mean()),
            float(np.abs(chroma(variant)[valid] - chroma(reference)[valid]).mean()),
            float((variant >= 0.999).any(axis=2).mean()))


def analyse(engine: str):
    aggregates = {"bare": [], "suffix_2": []}
    missing = []
    for image_id in IMAGE_IDS:
        reference_path, bare_path, suffix_2_path = paths(image_id, engine)
        if not all(path.exists() for path in (reference_path, bare_path, suffix_2_path)):
            missing.append(image_id)
            continue
        reference = load_linear_rgb(reference_path)
        aggregates["bare"].append(measures(reference, load_linear_rgb(bare_path)))
        aggregates["suffix_2"].append(measures(reference, load_linear_rgb(suffix_2_path)))
    print(f"  {engine}: {len(IMAGE_IDS) - len(missing)} scenes")
    if missing:
        print("    Excluded: " + ", ".join(missing))
    for label, entries in aggregates.items():
        if entries:
            values = np.mean(entries, axis=0)
            print(f"    {label}: local_contrast_ratio={values[0]:.4f}; "
                  f"gradient_ratio={values[1]:.4f}; Y={values[2]:.4f}; "
                  f"mean_chroma_delta={values[3]:.4f}; clip={100.0 * values[4]:.3f}%")


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
            print("CLARITY")
            for engine_name in ("LR", "IPR"):
                analyse(engine_name)
    finally:
        if output_path:
            output.close()
