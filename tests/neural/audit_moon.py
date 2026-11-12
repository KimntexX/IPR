"""Read-only bounded CR3 metadata and selected embedded-JPEG audit.

No RAW unpack/demosaic, image output, training or averaging. CMT1/CMT2 static
TIFF fields only: not a full Canon MakerNotes/CTMD decoder. Explicit input folder;
no search for unrelated photographs. Existing burst movement evidence is reused.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

import cv2
import numpy as np
from PIL import Image
import rawpy

CANON_UUID = bytes.fromhex('85c0b687820f11e08111f4ce462b6a48')
TAGS = {0x010f:'make', 0x0110:'model', 0x0112:'orientation', 0x0132:'datetime',
        0x829a:'exposure_s', 0x829d:'aperture', 0x8827:'iso',
        0x9003:'datetime_original', 0x920a:'focal_mm', 0xa434:'lens'}


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as handle:
        for chunk in iter(lambda: handle.read(1024*1024), b''):
            h.update(chunk)
    return h.hexdigest()


def tiff_fields(data):
    if len(data) < 8 or len(data) > 2*1024*1024 or data[:2] not in (b'II', b'MM'):
        raise ValueError('Invalid bounded TIFF header')
    endian = '<' if data[:2] == b'II' else '>'
    if struct.unpack_from(endian+'H', data, 2)[0] != 42:
        raise ValueError('Not classic TIFF')
    offset = struct.unpack_from(endian+'I', data, 4)[0]
    seen, result = set(), {}
    for _ in range(8):
        if offset == 0:
            return result
        if offset in seen or offset < 8 or offset+2 > len(data):
            raise ValueError('Invalid/cyclic IFD')
        seen.add(offset)
        count = struct.unpack_from(endian+'H', data, offset)[0]
        if count > 4096 or offset+2+12*count+4 > len(data):
            raise ValueError('Invalid IFD extent')
        for i in range(count):
            pos = offset+2+12*i
            tag, kind, length = struct.unpack_from(endian+'HHI', data, pos)
            if tag not in TAGS or kind not in (2,3,4,5) or not 1 <= length <= 1024:
                continue
            size = {2:1,3:2,4:4,5:8}[kind]*length
            start = pos+8 if size <= 4 else struct.unpack_from(endian+'I', data, pos+8)[0]
            if start+size > len(data):
                raise ValueError('Tag outside TIFF')
            payload = data[start:start+size]
            if kind == 2:
                value = payload.rstrip(b'\0').decode('ascii', errors='replace')
            elif kind == 5:
                a,b = struct.unpack_from(endian+'II', payload)
                if b == 0:
                    raise ValueError('Zero rational denominator')
                value = a/b
            else:
                value = struct.unpack_from(endian+{3:'H',4:'I'}[kind], payload)[0]
            name = TAGS[tag]
            if name in result and result[name] != value:
                raise ValueError('Conflicting static field')
            result[name] = value
        offset = struct.unpack_from(endian+'I', data, offset+2+12*count)[0]
    raise ValueError('IFD depth exceeded')


def metadata(path):
    path = Path(path)
    before = path.stat()
    blocks = []
    with path.open('rb') as handle:
        def walk(start,end,depth=0):
            if depth > 8:
                raise ValueError('BMFF depth exceeded')
            cursor = start
            while cursor < end:
                if end-cursor < 8:
                    raise ValueError('Truncated box')
                handle.seek(cursor)
                size,kind = struct.unpack('>I4s', handle.read(8))
                header = 8
                if size == 1:
                    if end-cursor < 16:
                        raise ValueError('Truncated extended box')
                    size = struct.unpack('>Q',handle.read(8))[0]
                    header = 16
                if size == 0:
                    size = end-cursor
                if size < header or cursor+size > end:
                    raise ValueError('Invalid box extent')
                first,last = cursor+header,cursor+size
                if kind in (b'CMT1',b'CMT2'):
                    if last-first > 2*1024*1024:
                        raise ValueError('Oversized static metadata')
                    handle.seek(first)
                    data = handle.read(last-first)
                    blocks.append(dict(box=kind.decode(),sha256=hashlib.sha256(data).hexdigest(),
                                       fields=tiff_fields(data)))
                elif kind == b'moov':
                    walk(first,last,depth+1)
                elif kind == b'uuid' and last-first >= 16:
                    handle.seek(first)
                    if handle.read(16) == CANON_UUID:
                        walk(first+16,last,depth+1)
                cursor = last
        walk(0,before.st_size)
    after = path.stat()
    if (before.st_size,before.st_mtime_ns) != (after.st_size,after.st_mtime_ns):
        raise ValueError('Source changed during metadata read')
    fields = {}
    for block in blocks:
        for key,value in block['fields'].items():
            if key in fields and fields[key] != value:
                raise ValueError('Conflicting CMT fields')
            fields[key] = value
    return dict(path=str(path.resolve()),bytes=before.st_size,mtime_ns=before.st_mtime_ns,
                static_blocks=blocks,metadata=fields,
                complete_basic=all(k in fields for k in ('model','iso','exposure_s','aperture')))


def preview(path):
    # open_file + unpack_thumb only, never unpack() or postprocess().
    with rawpy.RawPy() as raw:
        raw.open_file(str(path))
        raw.unpack_thumb()
        thumb = raw.extract_thumb()
        if thumb.format != rawpy.ThumbFormat.JPEG:
            raise ValueError('Expected embedded JPEG')
        payload = thumb.data
    with Image.open(io.BytesIO(payload)) as im:
        if im.width*im.height > 80_000_000:
            raise ValueError('Oversized preview')
        rgb = np.asarray(im.convert('RGB'))
    gray = cv2.cvtColor(rgb,cv2.COLOR_RGB2GRAY)
    _, labels, stats, _ = cv2.connectedComponentsWithStats((gray >= 32).astype('uint8'),8)
    if len(stats) <= 1:
        return dict(status='no_bright_component',preview_shape=list(rgb.shape))
    x,y,w,h,area = [int(v) for v in stats[1:][np.argmax(stats[1:,4])]]
    selected = labels[y:y+h,x:x+w] == (1+int(np.argmax(stats[1:,4])))
    crop = rgb[y:y+h,x:x+w]
    luminance = gray[y:y+h,x:x+w]
    edge = cv2.Laplacian(luminance.astype('float32'),cv2.CV_32F)
    return dict(status='embedded_preview_only',preview_shape=list(rgb.shape),
                embedded_jpeg_sha256=hashlib.sha256(payload).hexdigest(),
                bright_component_threshold_encoded=32,component_box_xywh=[x,y,w,h],
                component_area_pixels=area,
                median_encoded_gray=float(np.median(luminance[selected])),
                p99_encoded_gray=float(np.percentile(luminance[selected],99)),
                encoded_channel_255_fraction=float((crop[selected] >= 255).mean()),
                laplacian_std_encoded=float(np.std(edge[selected])),
                source_sha256=digest(path),
                warning='JPEG processing affects every metric. Neither RAW clipping nor raw detail/noise measured; bbox uses preview coordinates only.')


def run(folder, historical_root, output_dir):
    cv2.setNumThreads(1)
    root = Path(__file__).resolve().parents[2]
    outroot = (root/'build-patch_neural_restoration').resolve()
    output_dir = output_dir.resolve()
    if not output_dir.is_relative_to(outroot):
        raise ValueError('Output outside dedicated bench build')
    files = sorted(folder.glob('*.CR3'))
    if not files or len(files) > 1000:
        raise ValueError('Expected 1..1000 explicitly scoped CR3 files')
    records = [metadata(p) for p in files]
    if not all(r['complete_basic'] for r in records):
        raise ValueError('Incomplete metadata: stop before pixel inspection')
    groups = Counter((r['metadata']['iso'],r['metadata']['exposure_s'],r['metadata']['aperture']) for r in records)
    chosen = [files[0],files[len(files)//2],files[-1]]
    previews = [dict(path=str(p.resolve()),**preview(p)) for p in chosen]
    for row in records:
        stat = Path(row['path']).stat()
        if (stat.st_size,stat.st_mtime_ns) != (row['bytes'],row['mtime_ns']):
            raise ValueError('Source changed during audit')
    historical = []
    for name in ('partition-rafale-bruit-v001-20261027T225326Z.json',
                 'mouvement-rafale-bruit-v001-20261027T225632Z.json'):
        path = historical_root/name
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        historical.append(dict(path=str(path.resolve()),sha256=digest(path),
                               rules=data.get('rules'),verdict=data.get('verdict'),
                               keys=list(data),summary={k:data[k] for k in ('summary','limitations','decision') if k in data}))
        if name.startswith('mouvement-'):
            rows = data['records']
            historical[-1]['summary'].update(
                measured_development_frames=len(rows),
                max_abs_rotation_degrees=max(abs(r['rotation_degrees']) for r in rows),
                min_scale=min(r['scale'] for r in rows),max_scale=max(r['scale'] for r in rows),
                max_translation_norm_at_800px_preview=max(
                    float(np.hypot(r['reference_to_frame_preview_affine'][0][2],
                                   r['reference_to_frame_preview_affine'][1][2])) for r in rows),
                note='Reused global affine diagnostics, not fresh pixel reads or proof of local alignment.')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1,created_utc=stamp,study='moon_static_metadata_and_preview_audit',
                  revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
                  source_sha256=digest(__file__),records=records,selected_previews=previews,
                  capture_groups=[dict(iso=k[0],exposure_s=k[1],aperture=k[2],count=v) for k,v in groups.items()],
                  versions=dict(numpy=np.__version__,opencv=cv2.__version__,rawpy=rawpy.__version__,libraw=list(rawpy.libraw_version)),
                  reused_historical_evidence=historical,
                  limits=['User confirms tripod; no RAW demosaic, alignment, mean reference or training.',
                          'Three selected embedded JPEGs only; not a survey of full-resolution RAW detail or saturation.',
                          'ISO/exposure/aperture are static CMT TIFF fields; no complete MakerNotes/CTMD or WB/bit-depth audit.',
                          'Only selected preview sources fully hashed; metadata-block hashes are not whole-file hashes.',
                          'Existing 42-frame burst history reused; no development/validation/sealed pixels read.',
                          'No Canon source copied, renamed, removed or overwritten.'])
    output_dir.mkdir(parents=True,exist_ok=True)
    destination = output_dir/f'moon-audit-v001-{stamp}.json'
    with destination.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(destination)
    print(json.dumps(dict(files=len(records),capture_groups=result['capture_groups'],previews=previews),indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--folder',type=Path,required=True)
    parser.add_argument('--historical-root',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,default=Path(__file__).resolve().parents[2]/'build-patch_neural_restoration')
    args = parser.parse_args()
    run(args.folder,args.historical_root,args.output_dir)
