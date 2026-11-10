"""Viewing sheets of the real-noise evaluation: noisy, VST + BM3D, network, reference.

System Python (numpy, Pillow, rawpy). One PNG per scene and ISO, four panels
side by side, one packed CFA cell per screen pixel (half the sensor
resolution: R, mean of the two greens, B), camera white balance of the
frame, one exposure for the four panels, sRGB curve. Reference: mean of the
other frames of the same burst. For visual review by the user only; the
JSON proofs hold the measurements.
"""
import argparse
from pathlib import Path

from prepare_next_training import digest, read, write
from real_noise_bench import CARD


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT/'build-patch_neural_restoration'
VIEW = (448, 640)


def to_rgb(planes, multipliers, scale):
    import numpy as np
    rgb = np.stack([planes[0], (planes[1]+planes[2])/2, planes[3]], axis=-1)*np.asarray(multipliers)*scale
    rgb = np.clip(rgb, 0, 1)
    return np.where(rgb <= .0031308, 12.92*rgb, 1.055*rgb**(1/2.4)-.055)


def run(evaluation, output):
    import numpy as np
    import rawpy
    from PIL import Image, ImageDraw, ImageFont
    font = ImageFont.truetype('arial.ttf', 16)
    crops, net, bm3d_meta, burst_meta = (read(evaluation/n) for n in ('crops.json', 'network.json', 'bm3d.json', 'burst.json'))
    arrays, networks, bm3ds, bursts = (np.load(evaluation/n) for n in ('crops.npz', 'network.npz', 'bm3d.npz', 'burst.npz'))
    output.mkdir(exist_ok=False)
    seed = net['seeds'][0]
    sheets = []
    for entry in crops['entries']:
        key = entry['key']
        frame = arrays[key+'-noisy'][1].astype(np.float64)
        others = (bursts[key+'-sum']-frame)/(burst_meta['counts'][key]-1)
        with rawpy.imread(str(CARD/entry['files'][1])) as raw:
            wb = np.asarray(raw.camera_whitebalance[:3], dtype=np.float64)
        wb = wb/wb[1]
        h, w = frame.shape[-2:]
        window = (slice((h-VIEW[0])//2, (h+VIEW[0])//2), slice((w-VIEW[1])//2, (w+VIEW[1])//2))
        panels = [('Bruité', frame), ('VST + BM3D', bm3ds[key][1]), (f'Réseau E (graine {seed})', networks[f'{key}-seed{seed}'][1]),
                  ('Référence (moyenne rafale)', others)]
        reference_rgb = np.stack([others[0], (others[1]+others[2])/2, others[3]], axis=-1)[window]*wb
        scale = .85/max(float(np.percentile(reference_rgb, 99.5)), 1e-6)
        sheet = Image.new('RGB', (VIEW[1]*4+30, VIEW[0]+28), 'white')
        draw = ImageDraw.Draw(sheet)
        for column, (label, planes) in enumerate(panels):
            rgb = to_rgb(np.asarray(planes, dtype=np.float64)[:, window[0], window[1]], wb, scale)
            sheet.paste(Image.fromarray((rgb*255+.5).astype(np.uint8)), (column*(VIEW[1]+10), 28))
            draw.text((column*(VIEW[1]+10)+4, 5), f'{label} - {entry["scene"]} ISO {entry["iso"]}', fill='black', font=font)
        path = output/f'{key}.png'
        sheet.save(path)
        sheets.append(dict(key=key, file=path.name, frame=entry['files'][1], sha256=digest(path)))
    write(output/'sheets.json', dict(schema_version=1, status='viewing_sheets_for_user_review', evaluation=str(evaluation.resolve()),
        view_cells=list(VIEW), seed=seed, sheets=sheets, tool_sha256=digest(Path(__file__)),
        note='One packed cell per pixel (half resolution), camera white balance, common exposure, sRGB curve'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evaluation', type=Path, default=BUILD/'real-noise-evaluation-v003')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.evaluation.resolve(), args.output.resolve())
