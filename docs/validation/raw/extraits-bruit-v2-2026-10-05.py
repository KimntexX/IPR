"""Diagnostic native 256px crops; no statistics or edits to source photographs.

Run from repository root. Derived PNGs are 8-bit sRGB display aids only.
All states share the fixed ROI from the corresponding measurement JSON.
"""
import json
import argparse
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tests'))
from analyse_color_mixer import native_pixels

raw = Path('docs/validation/raw')
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--subjects-only', action='store_true', help='Only add explicit skin/hair crops')
args = parser.parse_args()
results = {}
for name in ('mesure-bruit-v2-initial-2026-10-05-v1.json',
             'mesure-bruit-v2-etendu-2026-10-05-v1.json'):
    results.update(json.loads((raw / name).read_text(encoding='utf-8'))['results'])

for image_id in (() if args.subjects_only else ('6V1A0112', '6V1A0624', '6V1A9890', '6V1A1020', '6V1A9734', '6V1A9421')):
    output = raw / f'extraits-bruit-v2-{image_id}-2026-10-05.png'
    if output.exists():
        raise SystemExit(f'Refusing to overwrite {output}')
    canvas = Image.new('RGB', (3 * 270, 6 * 292), '#222222')
    draw = ImageDraw.Draw(canvas)
    for component_index, component in enumerate(('luminance', 'couleur')):
        group = results[image_id][f'IPR-{component}']
        # First flat, first two detail ROIs: not a hand-picked best outcome.
        regions = [group['regions'][0]] + [r for r in group['regions'] if r['kind'] == 'detail'][:2]
        variants = {entry['value']: entry['path'] for entry in group['variants']}
        for column, value in enumerate((0, 50, 100)):
            source = Path(group['baseline_path'] if value == 0 else variants[value])
            pixels = native_pixels(source)
            for row, roi in enumerate(regions):
                y, x, size = roi['top'], roi['left'], roi['size']
                crop = np.asarray(pixels[y:y+size, x:x+size, :3], dtype=np.float32) / 65535.
                display = Image.fromarray(np.round(crop * 255.).astype(np.uint8))
                cell_x, cell_y = column * 270, (component_index * 3 + row) * 292
                draw.text((cell_x + 5, cell_y + 3), f'{image_id} {component} {value}', fill='white')
                draw.text((cell_x + 5, cell_y + 16), f"{roi['kind']} x={x} y={y}", fill='white')
                canvas.paste(display, (cell_x + 5, cell_y + 33))
    canvas.save(output)
    print(output)

# Explicit visual subject ROIs, separate from automatic statistical sampling.
image_id = '6V1A9734'
output = raw / f'extraits-bruit-v2-{image_id}-carnation-2026-10-05.png'
if output.exists():
    raise SystemExit(f'Refusing to overwrite {output}')
canvas = Image.new('RGB', (3 * 270, 6 * 292), '#222222')
draw = ImageDraw.Draw(canvas)
for component_index, component in enumerate(('luminance', 'couleur')):
    group = results[image_id][f'IPR-{component}']
    variants = {entry['value']: entry['path'] for entry in group['variants']}
    for column, value in enumerate((0, 50, 100)):
        pixels = native_pixels(Path(group['baseline_path'] if value == 0 else variants[value]))
        for row, (label, x, y) in enumerate((('hair-face', 2800, 2580), ('skin-arm', 2310, 3300), ('fabric', 1995, 4515))):
            crop = np.asarray(pixels[y:y+256, x:x+256, :3], dtype=np.float32) / 65535.
            display = Image.fromarray(np.round(crop * 255.).astype(np.uint8))
            cell_x, cell_y = column * 270, (component_index * 3 + row) * 292
            draw.text((cell_x+5, cell_y+3), f'{image_id} {component} {value}', fill='white')
            draw.text((cell_x+5, cell_y+16), f'{label} x={x} y={y}', fill='white')
            canvas.paste(display, (cell_x+5, cell_y+33))
canvas.save(output)
print(output)
