"""Embedded RAW thumbnail contact sheet for corpus selection, never measurement."""
import argparse
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageOps
import rawpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--scene', action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('Output exists')
    frames = json.loads(args.inventory.read_text(encoding='utf-8'))['frames']
    selected = []
    for stem in args.scene:
        matches = [r for r in frames if Path(r['path']).stem == stem]
        if not matches:
            raise SystemExit('Missing scene: ' + stem)
        selected.append(matches[0])
    width, height, columns = 320, 245, 4
    sheet = Image.new('RGB', (width * columns, height * ((len(selected) + columns - 1) // columns)), '#252a30')
    draw = ImageDraw.Draw(sheet)
    for i, frame in enumerate(selected):
        path = Path(frame['path'])
        with rawpy.imread(str(path)) as raw:
            thumb = raw.extract_thumb()
        if thumb.format == rawpy.ThumbFormat.JPEG:
            with Image.open(io.BytesIO(thumb.data)) as handle:
                preview = ImageOps.exif_transpose(handle).convert('RGB')
        else:
            preview = Image.fromarray(thumb.data)
        preview.thumbnail((width - 12, height - 45))
        x, y = i % columns * width, i // columns * height
        sheet.paste(preview, (x + (width - preview.width) // 2, y))
        metadata = frame['metadata']
        draw.text((x + 5, y + height - 40), path.name + ' ISO ' + str(metadata.get('iso')), fill='white')
        draw.text((x + 5, y + height - 23), str(metadata.get('model')), fill='white')
        print(path, flush=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.output)


if __name__ == '__main__':
    main()
