"""Inventory a bounded capture series and inspect embedded-JPEG stability.

No RAW development, training, source copying, averaging or deletion. JPEG
registration is a screening measure, not proof of RAW pair equivalence.
"""
import argparse
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import subprocess

import cv2
import numpy as np
from PIL import Image, ImageDraw
import rawpy

from audit_moon import digest, metadata


def small_preview(path):
    with rawpy.RawPy() as raw:
        raw.open_file(str(path))
        raw.unpack_thumb()
        thumb = raw.extract_thumb()
        if thumb.format != rawpy.ThumbFormat.JPEG:
            raise ValueError('Expected an embedded JPEG')
        payload = thumb.data
    with Image.open(io.BytesIO(payload)) as image:
        original_size = list(image.size)
        image.draft('RGB', (1000, 1000))
        image.thumbnail((800, 800))
        rgb = image.convert('RGB')
    return rgb, original_size


def run(folder, first, last, output):
    cv2.setNumThreads(2)
    root = Path(__file__).resolve().parents[2]
    output = output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Use the dedicated neural bench build')
    files = sorted(p for p in folder.glob('*.CR3') if first <= p.stem <= last)
    if not 1 <= len(files) <= 500:
        raise ValueError('Expected 1..500 explicitly bounded captures')
    output.mkdir(parents=True, exist_ok=False)
    records, groups = [], []
    previous = None
    for path in files:
        record = metadata(path)
        if not record['complete_basic']:
            raise ValueError('Incomplete basic metadata: ' + str(path))
        record['source_full_sha256'] = None
        records.append(record)
        m = record['metadata']
        key = tuple(m.get(k) for k in ('model','iso','exposure_s','aperture','focal_mm'))
        if key != previous:
            groups.append(dict(id=len(groups)+1, model=key[0], iso=key[1], exposure_s=key[2],
                               aperture=key[3], focal_mm=key[4], records=[]))
        groups[-1]['records'].append(len(records)-1)
        previous = key
    board = Image.new('RGB', (1200, 280*len(groups)), '#252525')
    draw = ImageDraw.Draw(board)
    for group_index, group in enumerate(groups):
        indices = group['records']
        selected = set((indices[0], indices[len(indices)//2], indices[-1]))
        evidence = []
        reference = None
        thumb_index = 0
        for index in indices:
            path = Path(records[index]['path'])
            rgb, original_size = small_preview(path)
            gray = cv2.cvtColor(np.asarray(rgb), cv2.COLOR_RGB2GRAY).astype('float32') / 255.
            if reference is None:
                reference = gray
            if gray.shape != reference.shape:
                raise ValueError('Changing preview dimensions within a capture group')
            transform = np.eye(2, 3, dtype='float32')
            try:
                ecc, transform = cv2.findTransformECC(reference, gray, transform, cv2.MOTION_EUCLIDEAN,
                    (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 60, 1e-5), None, 5)
                alignment = dict(status='measured', ecc=float(ecc),
                    reference_to_frame_preview_affine=transform.tolist(),
                    translation_pixels=float(np.linalg.norm(transform[:,2])),
                    rotation_degrees=float(np.degrees(np.arctan2(transform[1,0],transform[0,0]))))
            except cv2.error as error:
                alignment = dict(status='failed', reason=str(error))
            evidence.append(dict(path=str(path), preview_size_wh=list(rgb.size),
                original_jpeg_size_wh=original_size,
                gray_mean=float(gray.mean()), gray_percentiles=np.percentile(gray,[1,50,99]).tolist(),
                laplacian_std=float(cv2.Laplacian(gray,cv2.CV_32F).std()), alignment=alignment))
            if index in selected:
                records[index]['source_full_sha256'] = digest(path)
                tile = rgb.copy()
                tile.thumbnail((400,245))
                left, top = thumb_index*400, group_index*280
                board.paste(tile, (left,top+30))
                draw.text((left+5,top+8), f'G{group["id"]} {path.stem} ISO {group["iso"]}', fill='white')
                thumb_index += 1
        group['preview_evidence'] = evidence
        measured = [e['alignment'] for e in evidence if e['alignment']['status']=='measured']
        group['summary'] = dict(count=len(indices), first=Path(records[indices[0]]['path']).name,
            last=Path(records[indices[-1]]['path']).name, alignment_failures=len(indices)-len(measured),
            minimum_ecc=min((e['ecc'] for e in measured), default=None),
            maximum_translation_preview_pixels=max((e['translation_pixels'] for e in measured), default=None),
            maximum_absolute_rotation_degrees=max((abs(e['rotation_degrees']) for e in measured), default=None),
            minimum_mean_gray=min(e['gray_mean'] for e in evidence),
            maximum_mean_gray=max(e['gray_mean'] for e in evidence))
        print(json.dumps(dict(group=group['id'], iso=group['iso'], exposure_s=group['exposure_s'],
                             focal_mm=group['focal_mm'], **group['summary'])), flush=True)
    board.save(output/'series-overview.jpg', quality=92)
    for r in records:
        now = Path(r['path']).stat()
        if (now.st_size, now.st_mtime_ns) != (r['bytes'],r['mtime_ns']):
            raise ValueError('Source changed during audit')
    measured = datetime.now(timezone.utc)
    campaign = 'neural-capture-series-v001-' + measured.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1, data_version=1, campaign_id=campaign,
        measured_at_utc=measured.isoformat(), study='bounded_static_metadata_and_jpeg_stability',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        tool_sha256=digest(__file__), metadata_tool_sha256=digest(root/'tests/neural/audit_moon.py'),
        source_range=dict(folder=str(folder.resolve()),first=first,last=last),
        versions=dict(rawpy=rawpy.__version__, libraw=list(rawpy.libraw_version), numpy=np.__version__,opencv=cv2.__version__),
        groups=groups, records=records,
        limits=['Group boundaries follow consecutive identical basic metadata, not a verified scene identity.',
                'ECC Euclidean alignment is measured on processed embedded JPEGs at maximum 800px.',
                'Local motion, RAW clipping, RAW noise, focus changes and usable clean references remain unqualified.',
                'JPEG encoded gray values are not linear radiometry or calibrated luminance.',
                'Only selected first/middle/last files have full-file SHA-256; all files have static-block hashes.',
                'Sources unchanged; no RAW decoding development, reference averaging or training.'])
    for destination in (output/'audit.json', root/'docs/validation/raw'/f'{campaign}.json'):
        with destination.open('x',encoding='utf8') as stream:
            json.dump(result,stream,ensure_ascii=False,indent=2,allow_nan=False)
            stream.write('\n')
    print('Evidence: ' + campaign, flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder',type=Path,required=True)
    parser.add_argument('--first',required=True)
    parser.add_argument('--last',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(args.folder,args.first,args.last,args.output)
