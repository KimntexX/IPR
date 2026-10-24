"""Read-only Canon RAW metadata inventory; no full RAW decode or source writes."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct

from audit_noise_pairs import CANON_UUID, tiff_fields


def cr3_metadata(path):
    result = {}
    size = path.stat().st_size
    with path.open('rb') as stream:
        def walk(first, last, depth=0):
            if depth > 12:
                raise ValueError('Container nesting limit')
            position = first
            while position + 8 <= last:
                stream.seek(position)
                header = stream.read(8)
                length, kind = struct.unpack('>I4s', header)
                offset = 8
                if length == 1:
                    length = struct.unpack('>Q', stream.read(8))[0]
                    offset = 16
                elif length == 0:
                    length = last - position
                if length < offset or position + length > last:
                    raise ValueError('Invalid box extent')
                payload, end = position + offset, position + length
                if kind in (b'CMT1', b'CMT2'):
                    if end - payload > 1024 * 1024:
                        raise ValueError('Unexpected metadata size')
                    stream.seek(payload)
                    result.update(tiff_fields(stream.read(end - payload)))
                elif kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
                    walk(payload, end, depth + 1)
                elif kind == b'uuid':
                    stream.seek(payload)
                    if stream.read(16) == CANON_UUID:
                        walk(payload + 16, end, depth + 1)
                position = end
        walk(0, size)
    return result


def cr2_metadata(path):
    names = {0x10f: 'make', 0x110: 'model', 0x132: 'datetime', 0x9003: 'datetime_original',
             0x829a: 'exposure_s', 0x829d: 'aperture', 0x8827: 'iso', 0x920a: 'focal_mm', 0xa434: 'lens'}
    result = {}
    size = path.stat().st_size
    with path.open('rb') as stream:
        header = stream.read(8)
        if header[:2] not in (b'II', b'MM'):
            raise ValueError('Not TIFF-based CR2')
        endian = '<' if header[:2] == b'II' else '>'
        if struct.unpack_from(endian + 'H', header, 2)[0] != 42:
            raise ValueError('Not classic TIFF-based CR2')
        def ifd(offset, depth=0):
            if depth > 2 or offset + 2 > size:
                raise ValueError('Invalid IFD')
            stream.seek(offset)
            count = struct.unpack(endian + 'H', stream.read(2))[0]
            if count > 1024 or offset + 2 + count * 12 > size:
                raise ValueError('IFD extent')
            entries = stream.read(count * 12)
            for i in range(count):
                entry = entries[i * 12:(i + 1) * 12]
                tag, kind, n, pointer = struct.unpack(endian + 'HHII', entry)
                if tag == 0x8769:
                    ifd(pointer, depth + 1)
                    continue
                if tag not in names or kind not in (2, 3, 4, 5) or not 0 < n <= 1024:
                    continue
                length = {2: 1, 3: 2, 4: 4, 5: 8}[kind] * n
                if length <= 4:
                    payload = entry[8:8 + length]
                else:
                    if pointer + length > size:
                        raise ValueError('Tag extent')
                    stream.seek(pointer)
                    payload = stream.read(length)
                if kind == 2:
                    value = payload.split(b'\0')[0].decode('ascii', errors='replace')
                elif kind in (3, 4):
                    value = struct.unpack_from(endian + ('H' if kind == 3 else 'I'), payload)[0]
                else:
                    numerator, denominator = struct.unpack_from(endian + 'II', payload)
                    value = numerator / denominator if denominator else None
                result[names[tag]] = value
        ifd(struct.unpack_from(endian + 'I', header, 4)[0])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--data-version', type=int, default=1)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('Output exists, nothing overwritten')
    records, errors, seen = [], [], set()
    for root in args.root:
        if not root.is_dir():
            errors.append({'path': str(root), 'error': 'Root unavailable'})
            continue
        for path in sorted(root.rglob('*')):
            if path.suffix.lower() not in ('.cr3', '.cr2') or not path.is_file():
                continue
            key = str(path.resolve()).casefold()
            if key in seen:
                continue
            seen.add(key)
            try:
                before = path.stat()
                metadata = cr3_metadata(path) if path.suffix.lower() == '.cr3' else cr2_metadata(path)
                after = path.stat()
                if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                    raise ValueError('Source changed while reading')
                records.append({'path': str(path), 'bytes': before.st_size,
                                'mtime_ns': before.st_mtime_ns, 'metadata': metadata})
            except (OSError, ValueError, struct.error) as error:
                errors.append({'path': str(path), 'error': str(error)})
    stamp = datetime.now(timezone.utc)
    result = {'campaign_id': f'inventaire-corpus-bruit-v{args.data_version:03d}-' + stamp.strftime('%Y%m%dT%H%M%SZ'),
              'schema_version': 1, 'data_version': args.data_version, 'measured_at_utc': stamp.isoformat(),
              'runner': Path(__file__).name,
              'runner_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'roots': list(map(str, args.root)), 'frames': records, 'errors': errors,
              'limitations': ['Metadata-only inventory, no decoded pixel quality or capture-pair validation',
                              'Different paths may contain identical files; no content deduplication claimed',
                              'Source hashes will be computed for selected files, not the entire stock']}
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=1, allow_nan=False)
    print(args.output, 'frames', len(records), 'errors', len(errors), flush=True)
    print(Counter((x['metadata'].get('model'), x['metadata'].get('iso')) for x in records))


if __name__ == '__main__':
    main()
