#!/usr/bin/env python3
"""Educational image lab, Python standard library only; NOT a production codec.

Default: deterministic demonstrations and self-tests; no files written.
--output-dir NEW_DIRECTORY creates three tiny synthetic files, never overwrites.
--inspect FILE reads size and first 64 bytes only, leaving source untouched.
PNG decoder intentionally supports only non-interlaced RGB8, bounded to 1 MiB
of decompressed data. It rejects everything else instead of guessing.
"""
import argparse
import binascii
import math
from pathlib import Path
import struct
import sys
import unittest
import zlib

SIGNATURE = b'\x89PNG\r\n\x1a\n'
MAX_DECODED = 1 << 20


def srgb_encode(value):
    return 12.92*value if value <= .0031308 else 1.055*value**(1/2.4)-.055


def srgb_decode(value):
    return value/12.92 if value <= .04045 else ((value+.055)/1.055)**2.4


def pack_msb(values, bits):
    """Simple contiguous MSB-first packing, not a Canon/Sony/Nikon convention."""
    if not 1 <= bits <= 16:
        raise ValueError('Expected 1..16 bits')
    accumulator = available = 0
    out = bytearray()
    for value in values:
        if not isinstance(value, int) or not 0 <= value < 1 << bits:
            raise ValueError('Sample outside declared bit depth')
        accumulator = (accumulator << bits) | value
        available += bits
        while available >= 8:
            available -= 8
            out.append((accumulator >> available) & 255)
    if available:
        out.append((accumulator << (8-available)) & 255)
    return bytes(out)


def unpack_msb(data, bits, count):
    if not 1 <= bits <= 16 or count < 0 or len(data) != (bits*count+7)//8:
        raise ValueError('Invalid packed sample length')
    accumulator = available = 0
    result = []
    for byte in data:
        accumulator = (accumulator << 8) | byte
        available += 8
        while available >= bits and len(result) < count:
            available -= bits
            result.append((accumulator >> available) & ((1 << bits)-1))
    if available and accumulator & ((1 << available)-1):
        raise ValueError('Nonzero padding in this teaching format')
    return result


def paeth(a, b, c):
    p = a+b-c
    distances = (abs(p-a), abs(p-b), abs(p-c))
    return (a, b, c)[distances.index(min(distances))]


def predictor(kind, left, above, upper_left):
    if kind == 0:
        return 0
    if kind == 1:
        return left
    if kind == 2:
        return above
    if kind == 3:
        return (left+above)//2
    if kind == 4:
        return paeth(left, above, upper_left)
    raise ValueError('Unknown PNG filter')


def filter_row(row, previous, bpp, kind, inverse=False):
    if len(row) != len(previous) or bpp <= 0:
        raise ValueError('Invalid row geometry')
    out = bytearray(len(row))
    for i, value in enumerate(row):
        # Encoder predicts from original row; decoder predicts from restored row.
        restored = out if inverse else row
        left = restored[i-bpp] if i >= bpp else 0
        above = previous[i]
        upper_left = previous[i-bpp] if i >= bpp else 0
        p = predictor(kind, left, above, upper_left)
        out[i] = (value+p if inverse else value-p) & 255
    return bytes(out)


def chunk(kind, payload):
    return (struct.pack('>I', len(payload))+kind+payload+
            struct.pack('>I', binascii.crc32(kind+payload) & 0xffffffff))


def make_png(width, height, rgb, filter_kind=0):
    if width <= 0 or height <= 0 or len(rgb) != width*height*3:
        raise ValueError('Invalid RGB8 geometry')
    if height*(1+3*width) > MAX_DECODED:
        raise ValueError('Teaching size limit exceeded')
    previous = bytes(3*width)
    raw = bytearray()
    for y in range(height):
        row = rgb[y*3*width:(y+1)*3*width]
        raw.append(filter_kind)
        raw.extend(filter_row(row, previous, 3, filter_kind))
        previous = row
    ihdr = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return SIGNATURE+chunk(b'IHDR', ihdr)+chunk(b'IDAT', zlib.compress(raw))+chunk(b'IEND', b'')


def read_png(data):
    """Restricted teaching reader with CRC, ordering and output-size checks."""
    if not data.startswith(SIGNATURE) or len(data) > 4*MAX_DECODED:
        raise ValueError('Invalid signature or teaching file size limit')
    position = 8
    header = None
    compressed = bytearray()
    seen_idat = finished = False
    while position < len(data):
        if len(data)-position < 12:
            raise ValueError('Truncated chunk')
        size = struct.unpack_from('>I', data, position)[0]
        kind = data[position+4:position+8]
        if size > len(data)-position-12:
            raise ValueError('Chunk outside file')
        payload = data[position+8:position+8+size]
        crc = struct.unpack_from('>I', data, position+8+size)[0]
        if crc != binascii.crc32(kind+payload) & 0xffffffff:
            raise ValueError('CRC mismatch')
        position += size+12
        if header is None and kind != b'IHDR':
            raise ValueError('IHDR must be first')
        if kind == b'IHDR':
            if header is not None or size != 13:
                raise ValueError('Duplicate or malformed IHDR')
            header = struct.unpack('>IIBBBBB', payload)
        elif kind == b'IDAT':
            compressed.extend(payload)
            seen_idat = True
        elif kind == b'IEND':
            if size or not seen_idat or position != len(data):
                raise ValueError('Invalid IEND or trailing bytes')
            finished = True
            break
        else:
            # Intentional subset: no unknown chunks, even optional ones.
            raise ValueError('Chunk outside teaching subset')
    if header is None or not finished:
        raise ValueError('Incomplete PNG')
    width, height, depth, color, compression, filtering, interlace = header
    expected = height*(1+width*3)
    if (width == 0 or height == 0 or expected > MAX_DECODED or
            (depth, color, compression, filtering, interlace) != (8, 2, 0, 0, 0)):
        raise ValueError('Unsupported or excessive RGB8 geometry')
    decoder = zlib.decompressobj()
    raw = decoder.decompress(bytes(compressed), expected+1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError('Invalid zlib stream or decompressed size')
    result = bytearray()
    previous = bytes(width*3)
    for y in range(height):
        start = y*(1+width*3)
        previous = filter_row(raw[start+1:start+1+width*3], previous, 3, raw[start], True)
        result.extend(previous)
    return width, height, bytes(result)


def make_tiff(width, height, rgb):
    """One RGB8, little-endian, uncompressed strip, no ICC/Exif; classic TIFF."""
    if width <= 0 or height <= 0 or len(rgb) != width*height*3:
        raise ValueError('Invalid RGB8 geometry')
    count = 9
    bits_offset = 8+2+12*count+4
    pixels_offset = bits_offset+6
    entries = [(256, 4, 1, width), (257, 4, 1, height),
               (258, 3, 3, bits_offset), (259, 3, 1, 1), (262, 3, 1, 2),
               (273, 4, 1, pixels_offset), (277, 3, 1, 3),
               (278, 4, 1, height), (279, 4, 1, len(rgb))]
    directory = struct.pack('<H', count)
    for tag, kind, n, value in entries:
        directory += struct.pack('<HHII', tag, kind, n, value)
    directory += struct.pack('<I', 0)
    return b'II'+struct.pack('<HI', 42, 8)+directory+struct.pack('<HHH', 8, 8, 8)+rgb


def dct8(block, inverse=False):
    if len(block) != 8 or any(len(row) != 8 for row in block):
        raise ValueError('Expected 8x8 block')
    basis = [[(.5/math.sqrt(2) if u == 0 else .5)*
              math.cos((2*x+1)*u*math.pi/16) for x in range(8)] for u in range(8)]
    if inverse:
        return [[sum(basis[u][x]*basis[v][y]*block[u][v]
                     for u in range(8) for v in range(8)) for y in range(8)] for x in range(8)]
    return [[sum(basis[u][x]*basis[v][y]*block[x][y]
                 for x in range(8) for y in range(8)) for v in range(8)] for u in range(8)]


class ImageLabTests(unittest.TestCase):
    def test_packed_12_bit_known_bytes(self):
        self.assertEqual(pack_msb([0xabc, 0x123], 12), bytes.fromhex('ab c1 23'))

    def test_packing_roundtrip_and_limits(self):
        for bits in (1, 8, 10, 12, 14, 16):
            values = [0, 1, (1 << bits)-1, (1 << bits)//2, 0]
            self.assertEqual(unpack_msb(pack_msb(values, bits), bits, len(values)), values)
        with self.assertRaises(ValueError):
            pack_msb([4096], 12)
        with self.assertRaises(ValueError):
            unpack_msb(b'\xff', 12, 1)

    def test_png_filter_roundtrip_all_five(self):
        row = bytes([0, 255, 1, 128, 254, 10, 3, 250, 20])
        previous = bytes([100, 0, 255, 10, 11, 12, 40, 50, 60])
        for kind in range(5):
            residual = filter_row(row, previous, 3, kind)
            self.assertEqual(filter_row(residual, previous, 3, kind, True), row)

    def test_png_roundtrip(self):
        rgb = bytes((i*37) & 255 for i in range(27))
        for kind in range(5):
            self.assertEqual(read_png(make_png(3, 3, rgb, kind)), (3, 3, rgb))

    def test_png_multiple_idat_share_one_zlib_stream(self):
        rgb = bytes([255, 0, 0, 0, 255, 0])
        ihdr = struct.pack('>IIBBBBB', 2, 1, 8, 2, 0, 0, 0)
        payload = zlib.compress(b'\0'+rgb)
        data = SIGNATURE+chunk(b'IHDR', ihdr)
        data += chunk(b'IDAT', payload[:3])+chunk(b'IDAT', payload[3:])+chunk(b'IEND', b'')
        self.assertEqual(read_png(data), (2, 1, rgb))

    def test_png_rejects_corruption_truncation_and_trailing_bytes(self):
        data = make_png(1, 1, bytes([20, 40, 80]))
        bad = bytearray(data)
        bad[20] ^= 1
        for candidate in (bytes(bad), data[:-1], data+b'junk'):
            with self.assertRaises(ValueError):
                read_png(candidate)

    def test_png_bounded_decompression(self):
        ihdr = struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)
        data = SIGNATURE+chunk(b'IHDR', ihdr)+chunk(b'IDAT', zlib.compress(bytes(10000)))+chunk(b'IEND', b'')
        with self.assertRaises(ValueError):
            read_png(data)

    def test_tiff_offsets_and_payload(self):
        rgb = bytes([255, 0, 0, 0, 255, 0])
        data = make_tiff(2, 1, rgb)
        self.assertEqual(data[:8], bytes.fromhex('49 49 2a 00 08 00 00 00'))
        self.assertEqual(len(data), 134)
        entries = [struct.unpack_from('<HHII', data, 10+12*i) for i in range(9)]
        tags = {tag: (kind, count, value) for tag, kind, count, value in entries}
        self.assertEqual(tags[256][2], 2)
        self.assertEqual(data[tags[273][2]:], rgb)
        self.assertEqual(struct.unpack_from('<HHH', data, tags[258][2]), (8, 8, 8))

    def test_dct_constant_block_and_inverse(self):
        block = [[12.]*8 for _ in range(8)]
        coefficients = dct8(block)
        self.assertAlmostEqual(coefficients[0][0], 96)
        self.assertLess(max(abs(coefficients[u][v]) for u in range(8) for v in range(8) if (u, v) != (0, 0)), 1e-12)
        restored = dct8(coefficients, True)
        self.assertLess(max(abs(x-12) for row in restored for x in row), 1e-12)

    def test_srgb_order_and_roundtrip(self):
        for value in (0, .001, .01, .18, .5, 1):
            self.assertAlmostEqual(srgb_decode(srgb_encode(value)), value, places=12)
        self.assertAlmostEqual(srgb_decode(.5), .21404114048223255)
        self.assertGreater(srgb_encode(.5), .73)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--inspect', type=Path)
    args = parser.parse_args()
    if args.inspect:
        with args.inspect.open('rb') as stream:
            prefix = stream.read(64)
        print('File:', args.inspect, 'bytes:', args.inspect.stat().st_size)
        for offset in range(0, len(prefix), 16):
            print(f'{offset:08x}:', prefix[offset:offset+16].hex(' '))
    print('12-bit MSB example:', pack_msb([0xabc, 0x123], 12).hex(' '))
    print('sRGB 0.5 -> linear:', srgb_decode(.5))
    print('Linear 0.5 -> sRGB:', srgb_encode(.5))
    print('DCT constant 12 -> DC:', dct8([[12.]*8 for _ in range(8)])[0][0])
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=False)
        rgb = bytes([255, 0, 0, 0, 255, 0])
        files = {'deux-pixels.ppm': b'P6\n2 1\n255\n'+rgb,
                 'deux-pixels.png': make_png(2, 1, rgb),
                 'deux-pixels.tiff': make_tiff(2, 1, rgb)}
        for name, data in files.items():
            with (args.output_dir/name).open('xb') as stream:
                stream.write(data)
            print(name, len(data), 'bytes')
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ImageLabTests))
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
