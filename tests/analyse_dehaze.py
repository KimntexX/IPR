#!/usr/bin/env python3
"""Compare dehaze variants against neutral Lightroom and IPR TIFFs."""

from contextlib import redirect_stdout
from pathlib import Path
import sys

import numpy as np
from PIL import Image


ROOT = Path("tests/image/base")
NEUTRAL_ROOT = ROOT / "neutre"
DEHAZE_ROOT = ROOT / "correction_de_voile"
IMAGE_IDS = ("6V1A0007", "6V1A0043", "6V1A0767", "6V1A9073",
             "6V1A9119", "6V1A9734", "6V1A9944", "6V1A9947")
SAMPLE_SIZE = (580, 870)
LUMINANCE = np.array((0.2126, 0.7152, 0.0722), dtype=np.float32)


def load_linear_rgb(path: Path) -> np.ndarray:
    with Image.open(path) as image:
        encoded = np.asarray(image.convert("RGB").resize(SAMPLE_SIZE, Image.Resampling.BOX),
                             dtype=np.float32) / 255.0
    return np.where(encoded <= 0.04045, encoded / 12.92,
                    ((encoded + 0.055) / 1.055) ** 2.4)


def paths(image_id: str, engine: str):
    if engine == "LR":
        return (NEUTRAL_ROOT / f"{image_id}-3.tif",
                DEHAZE_ROOT / f"{image_id}.tif", DEHAZE_ROOT / f"{image_id}-2.tif")
    return (NEUTRAL_ROOT / f"{image_id}-IPR-3.tiff",
            DEHAZE_ROOT / f"{image_id}-IPR.tiff",
            DEHAZE_ROOT / f"{image_id}-IPR-2.tiff")


def chroma(rgb: np.ndarray) -> np.ndarray:
    maximum = rgb.max(axis=2)
    return (maximum - rgb.min(axis=2)) / np.maximum(maximum, 1e-5)


def measures(reference: np.ndarray, variant: np.ndarray):
    reference_luma, variant_luma = reference @ LUMINANCE, variant @ LUMINANCE
    ref_p05, ref_p50, ref_p95 = np.quantile(reference_luma, (0.05, 0.50, 0.95))
    var_p05, var_p50, var_p95 = np.quantile(variant_luma, (0.05, 0.50, 0.95))
    ref_contrast = ref_p95 - ref_p05
    var_contrast = var_p95 - var_p05
    valid = reference_luma > 0.01
    return (var_p05 / max(ref_p05, 1e-6), var_p50 / max(ref_p50, 1e-6),
            var_p95 / max(ref_p95, 1e-6), var_contrast / max(ref_contrast, 1e-6),
            float(chroma(variant)[valid].mean() - chroma(reference)[valid].mean()),
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
            print(f"    {label}: p05={values[0]:.4f}; p50={values[1]:.4f}; "
                  f"p95={values[2]:.4f}; contrast={values[3]:.4f}; "
                  f"delta_chroma={values[4]:.4f}; clip={100.0 * values[5]:.3f}%")


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
            print("DEHAZE")
            for engine_name in ("LR", "IPR"):
                analyse(engine_name)
    finally:
        if output_path:
            output.close()
