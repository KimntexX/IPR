"""Read-only single-scene qualification: RAW code statistics and embedded JPEG.

No training, RAW development or claim of a clean reference from one capture.
Decoder-reported black/white levels are diagnostics, not calibrated thresholds.
"""
import argparse
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw
import rawpy

from audit_moon import digest, metadata


def audit(source, output):
    source = source.resolve(strict=True)
    root = Path(__file__).resolve().parents[2]
    allowed = (root / 'build-patch_neural_restoration').resolve()
    output = output.resolve()
    if not output.is_relative_to(allowed):
        raise ValueError('Output must stay in the dedicated neural bench build')
    output.mkdir(parents=True, exist_ok=False)
    before = source.stat()
    record = metadata(source)
    with rawpy.imread(str(source)) as raw:
        mosaic = raw.raw_image_visible
        if mosaic is None or raw.raw_pattern is None or raw.raw_pattern.shape != (2, 2):
            raise ValueError('Expected a visible Bayer mosaic')
        planes = []
        for y in range(2):
            for x in range(2):
                channel = int(raw.raw_pattern[y, x])
                values = mosaic[y::2, x::2]
                black = raw.black_level_per_channel[channel]
                white = raw.white_level
                planes.append(dict(offset_yx=[y, x], channel=channel,
                    label=raw.color_desc[channel:channel+1].decode('ascii'),
                    count=values.size, minimum=int(values.min()), maximum=int(values.max()),
                    percentiles=dict(zip(['p1','p50','p99','p999'],
                        map(float, np.percentile(values, [1,50,99,99.9])))),
                    decoder_black=black, decoder_white=white,
                    fraction_at_or_above_decoder_white=float(np.mean(values >= white)),
                    fraction_at_or_below_decoder_black=float(np.mean(values <= black))))
        shape = list(mosaic.shape)
        thumb = raw.extract_thumb()
        if thumb.format != rawpy.ThumbFormat.JPEG:
            raise ValueError('Expected an embedded JPEG')
        payload = thumb.data
    (output / 'embedded.jpg').write_bytes(payload)
    with Image.open(io.BytesIO(payload)) as image:
        preview = image.convert('RGB')
        preview_shape = [preview.height, preview.width, 3]
        # Positions selected for this pilot only, on the 6960x4640 embedded JPEG.
        # Native-size crops: no resizing, no invented RAW/JPEG registration.
        crops = {'paper': (2600, 2400), 'yoga': (2000, 1400),
                 'orange': (3800, 2400), 'red': (5350, 1850),
                 'plush': (4500, 2700), 'aluminium': (2600, 3500),
                 'cardboard': (6400, 4000), 'printed_floor': (4100, 600)}
        if image.size == (6960, 4640):
            board = Image.new('RGB', (4*420, 2*450), '#252525')
            draw = ImageDraw.Draw(board)
            for index, (label, (x,y)) in enumerate(crops.items()):
                left, top = (index % 4)*420, (index // 4)*450
                board.paste(preview.crop((x,y,x+420,y+420)), (left,top+30))
                draw.text((left+6,top+8), label + ' JPEG 100%', fill='white')
            board.save(output / 'native-crops.jpg', quality=96)
        preview.thumbnail((1600, 1600))
        preview.save(output / 'overview.jpg', quality=94)
    after = source.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('Source changed during audit')
    measured = datetime.now(timezone.utc)
    result = dict(schema_version=1, data_version=1,
        campaign_id='neural-pilot-0133-v001-' + measured.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=measured.isoformat(),
        protocol='Single RAW read-only: CMT1/CMT2, Bayer code statistics, embedded JPEG native-size crops',
        units={'raw_values': 'decoder mosaic integer codes, no black subtraction or WB',
               'fractions': 'pixel count divided by per-plane count',
               'percentiles': 'linear NumPy percentiles of per-plane integer codes'},
        crop_boxes_embedded_jpeg_xywh={label: [x,y,420,420] for label,(x,y) in crops.items()}
            if preview_shape[:2] == [4640,6960] else {},
        study='neural_single_scene_pilot_qualification', source=record,
        source_sha256=digest(source), tool_sha256=digest(__file__),
        revision=subprocess.check_output(['git','rev-parse','HEAD'], cwd=root, text=True).strip(),
        rawpy=rawpy.__version__, libraw=list(rawpy.libraw_version),
        visible_mosaic_shape=shape, embedded_preview_shape=preview_shape, planes=planes,
        limitations=['Single capture: no independent noise truth or clean training target.',
            'Embedded JPEG has in-camera processing; sharpness review is not RAW noise measurement.',
            'White and black levels reported by LibRaw, not independently calibrated.',
            'Material identities supplied by the user; lino is printed texture, not a flat noise target.'])
    with (output / 'audit.json').open('x', encoding='utf8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    audit(args.source, args.output)
