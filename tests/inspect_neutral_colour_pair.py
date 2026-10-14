"""Diagnostic native colour statistics on independent image ROIs, never pixel MAE."""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

from analysis import native, metrics
from analyse_base_colorimetrique_v2 import srgb_lab


def statistics(encoded):
    linear = native.linearize(encoded)
    y = linear @ metrics.WEIGHTS
    lab, _ = srgb_lab(encoded)
    hue, saturation, _ = metrics.hsv(encoded)
    linear_hue, linear_saturation, _ = metrics.hsv(linear)
    lab_chroma = np.hypot(lab[..., 1], lab[..., 2])
    lab_hue = np.degrees(np.arctan2(lab[..., 2], lab[..., 1])) % 360
    # Independent broad warm mask: selection aid, not a flower/skin detector.
    warm = ((encoded[..., 0] > 1.1*encoded[..., 1]) &
            (encoded[..., 1] > 1.4*encoded[..., 2]) & (saturation > .25) & (y > .002))
    def summary(mask):
        if not mask.any():
            return {"samples": 0}
        chromatic = mask & (lab_chroma > 5)
        return {"samples": int(mask.sum()), "mean_Y": float(y[mask].mean()),
                "median_Y": float(np.median(y[mask])),
                "mean_Lab": lab[mask].mean(axis=0).tolist(),
                "median_Lab_chroma": float(np.median(lab_chroma[mask])),
                "mean_Lab_hue_degrees": metrics.circular_mean(lab_hue, chromatic),
                "mean_encoded_HSV_hue_degrees": metrics.circular_mean(hue, mask & (saturation > .1)),
                "mean_linear_HSV_hue_degrees": metrics.circular_mean(linear_hue, mask & (linear_saturation > .1)),
                "median_encoded_HSV_saturation": float(np.median(saturation[mask])),
                "mean_encoded_RGB": encoded[mask].mean(axis=0).tolist(),
                "mean_linear_RGB": linear[mask].mean(axis=0).tolist()}
    return {"whole_region": summary(np.ones(y.shape, dtype=bool)), "warm_mask": summary(warm)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ipr", type=Path)
    parser.add_argument("lr", type=Path)
    parser.add_argument("--roi", nargs=4, type=float, default=[0, 0, 1, 1], metavar=("X", "Y", "W", "H"))
    parser.add_argument("--step", type=int, default=4)
    parser.add_argument("--preview", type=Path)
    parser.add_argument("--reported-ipr-temperature", type=float)
    parser.add_argument("--reported-ipr-tint", type=float)
    parser.add_argument("--reported-ipr-zero-adjustments", action="store_true")
    parser.add_argument("--output-dir", type=Path, default=Path("docs/validation/raw"))
    args = parser.parse_args(argv)
    x, y, w, h = args.roi
    if args.step < 1 or min(x,y) < 0 or min(w,h) <= 0 or x+w > 1 or y+h > 1:
        parser.error("Positive step and a normalized ROI inside image required")
    records, inputs, previews = {}, [], []
    for engine, path in (("IPR", args.ipr), ("LR", args.lr)):
        pixels, metadata, before = native.inspect(path)
        height, width = pixels.shape[:2]
        box = (int(y*height), int(x*width), max(1,int(h*height)), max(1,int(w*width)))
        records[engine] = {"metadata": metadata, "declaration": native.embedded_metadata(path)[0],
                           "ROI_top_left_height_width": box,
                           "global": statistics(native.encoded(pixels, step=8)),
                           "ROI": statistics(native.encoded(pixels, box, args.step))}
        if args.preview:
            encoded = native.encoded(pixels, step=max(1, height//900))
            image = Image.fromarray(np.uint8(np.clip(encoded*255+.5,0,255)))
            image.thumbnail((650,900))
            draw = ImageDraw.Draw(image)
            draw.rectangle((int(x*image.width),int(y*image.height),int((x+w)*image.width),int((y+h)*image.height)), outline='white', width=2)
            previews.append((engine,image))
        inputs.append((path, before, metadata['sha256']))
    now = datetime.now(timezone.utc)
    result = {"schema_version": 1, "data_version": 1, "group": "couleur", "status": "diagnostic",
              "campaign_id": "inspection-neutre-couleur-v001-"+now.strftime("%Y%m%dT%H%M%S%fZ"),
              "measured_at_utc": now.isoformat(), "analyser_sha256": native.sha256(Path(__file__)),
              "method": {"space": "native uint16 sRGB, analytic linear Y and CIELAB D65",
                         "global_step":8, "ROI_step":args.step, "normalized_ROI_xywh":args.roi,
                         "warm_mask":"R>1.1G, G>1.4B, encoded HSV S>.25, linear Y>.002",
                         "geometry":"Independent ROIs and masks; no alignment, MAE or pixel DeltaE"},
              "records": records,
              "user_reported_IPR": {"temperature_kelvin":args.reported_ipr_temperature,
                                    "tint":args.reported_ipr_tint,
                                    "other_adjustments_zero":args.reported_ipr_zero_adjustments,
                                    "profile":"not independently confirmed",
                                    "normalization":"not independently confirmed",
                                    "source":"user statement, not embedded metadata"},
              "limitations": ["Warm mask is not a semantic segmentation", "Hue averages are not perceptual error scores",
                               "Manual IPR export may not declare settings, profile or normalization",
                               "Same normalized ROI is not guaranteed same optical content",
                               "No physical colour target: Lightroom agreement is not absolute accuracy"]}
    for path, before, digest in inputs:
        native.verify_unchanged(path, before, digest)
    target = args.output_dir/(result['campaign_id']+'.json')
    native.write_immutable(target,result)
    if args.preview:
        sheet = Image.new('RGB',(1300,950),'#24282d')
        draw = ImageDraw.Draw(sheet)
        for i,(name,img) in enumerate(previews):
            sheet.paste(img,(i*650+(650-img.width)//2,35))
            draw.text((i*650+15,12),name,fill='white')
        args.preview.parent.mkdir(parents=True,exist_ok=True)
        sheet.save(args.preview)
    print(target)
    for engine in records:
        print(engine,records[engine]['ROI'])
    return 0


if __name__ == '__main__':
    sys.exit(main())
