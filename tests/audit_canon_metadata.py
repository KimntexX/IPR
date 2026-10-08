#!/usr/bin/env python3
"""Bounded read-only CR3 CMT3/CTMD audit, not a general RAW decoder.

Offsets follow Clévy's CR3 description; ColorData candidates are raw words,
not certified field interpretations for an unknown camera/version.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

CANON_UUID = bytes.fromhex('85c0b687820f11e08111f4ce462b6a48')


def boxes(data, start, end):
    while start < end:
        if end - start < 8:
            raise ValueError('Truncated BMFF box header')
        size, kind = struct.unpack_from('>I4s', data, start)
        header = 8
        if size == 1:
            if end - start < 16:
                raise ValueError('Truncated extended box')
            size = struct.unpack_from('>Q', data, start + 8)[0]
            header = 16
        elif size == 0:
            size = end - start
        if size < header or start + size > end:
            raise ValueError('Invalid box extent')
        yield kind, start + header, start + size
        start += size


def tiff_color_data(data):
    if len(data) < 8 or data[:2] not in (b'II', b'MM'):
        raise ValueError('Not a TIFF header')
    endian = '<' if data[:2] == b'II' else '>'
    if struct.unpack_from(endian + 'H', data, 2)[0] != 42:
        raise ValueError('Not a classic TIFF')
    offset = struct.unpack_from(endian + 'I', data, 4)[0]
    if offset + 2 > len(data):
        raise ValueError('IFD outside TIFF')
    count = struct.unpack_from(endian + 'H', data, offset)[0]
    if offset + 2 + 12 * count + 4 > len(data):
        raise ValueError('Truncated IFD')
    for i in range(count):
        entry = offset + 2 + 12 * i
        tag, kind, length, location = struct.unpack_from(endian + 'HHII', data, entry)
        if tag != 0x4001:
            continue
        if kind != 3 or length < 4:
            raise ValueError('Unexpected ColorData type/length')
        location = entry + 8 if length * 2 <= 4 else location
        if location + length * 2 > len(data):
            raise ValueError('ColorData outside TIFF')
        words = struct.unpack_from(endian + str(length) + 'H', data, location)
        return {'word_count': length, 'version_word': words[0],
                'payload_sha256': hashlib.sha256(data[location:location + length * 2]).hexdigest(),
                'candidate_words_not_certified': {
                    hex(index): list(words[index:index + width])
                    for index, width in [(0x69, 5), (0x6e, 5), (0xcd, 5),
                                         (0x16b, 4), (0x17f, 4), (0x27c, 7), (0x290, 7)]
                    if index + width <= length}}
    return None


def audit(path):
    data = path.read_bytes()
    results = []
    def record(payload, source):
        value = tiff_color_data(payload)
        if value is not None:
            results.append({'source': source, **value})
    def walk(start, end, prefix):
        for kind, first, last in boxes(data, start, end):
            name = prefix + '/' + kind.decode('ascii', 'replace')
            if kind == b'CMT3':
                record(data[first:last], name)
            elif kind in (b'moov', b'mdia', b'minf', b'stbl'):
                walk(first, last, name)
            elif kind == b'uuid' and data[first:first + 16] == CANON_UUID:
                walk(first + 16, last, name)
            elif kind == b'trak':
                # Static CMT3 is also searched; timed metadata is read by tables.
                walk(first, last, name)
                tables = {}
                def collect(a, b):
                    for k, p, q in boxes(data, a, b):
                        if k in (b'mdia', b'minf', b'stbl'):
                            collect(p, q)
                        else:
                            tables[k] = data[p:q]
                collect(first, last)
                sd = tables.get(b'stsd', b'')
                if len(sd) < 16 or sd[12:16] != b'CTMD':
                    continue
                chunk = tables.get(b'co64') or tables.get(b'stco')
                sizes, mapping = tables[b'stsz'], tables[b'stsc']
                wide = b'co64' in tables
                n = struct.unpack_from('>I', chunk, 4)[0]
                offsets = struct.unpack_from('>' + ('Q' if wide else 'I') * n, chunk, 8)
                uniform, total = struct.unpack_from('>II', sizes, 4)
                lengths = [uniform] * total if uniform else struct.unpack_from('>' + 'I' * total, sizes, 12)
                nmap = struct.unpack_from('>I', mapping, 4)[0]
                maps = [struct.unpack_from('>III', mapping, 8 + 12 * j) for j in range(nmap)]
                index = 0
                for c, position in enumerate(offsets, 1):
                    valid = [m for m in maps if m[0] <= c]
                    if not valid:
                        raise ValueError('Missing chunk mapping')
                    for _ in range(valid[-1][1]):
                        if index >= total:
                            raise ValueError('Sample count overflow')
                        limit = position + lengths[index]
                        if limit > len(data):
                            raise ValueError('Sample outside file')
                        cursor = position
                        while cursor < limit:
                            if cursor + 12 > limit:
                                raise ValueError('Truncated CTMD record')
                            length, record_type = struct.unpack_from('<IH', data, cursor)
                            if length < 12 or cursor + length > limit:
                                raise ValueError('Invalid CTMD extent')
                            if record_type in (7, 8, 9):
                                part = cursor + 12
                                while part < cursor + length:
                                    size, tag = struct.unpack_from('<II', data, part)
                                    if size < 8 or part + size > cursor + length:
                                        raise ValueError('Invalid CTMD TIFF extent')
                                    if tag == 0x927c:
                                        record(data[part + 8:part + size], f'{name}/CTMD/sample{index}/record{record_type}')
                                    part += size
                            cursor += length
                        position = limit
                        index += 1
                if index != total:
                    raise ValueError('Incomplete CTMD sample map')
    walk(0, len(data), '')
    return {'raw': str(path), 'raw_sha256': hashlib.sha256(data).hexdigest(),
            'color_data_records': results, 'limitations': 'Known offsets exposed as candidates; no full MakerNotes semantics or RAW pixel decoding.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('raws', nargs='+', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite existing measurements')
    result = {'schema': 1, 'script_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              'images': [audit(path) for path in args.raws]}
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
    for entry in result['images']:
        print(Path(entry['raw']).stem, entry['color_data_records'], flush=True)


if __name__ == '__main__':
    main()
