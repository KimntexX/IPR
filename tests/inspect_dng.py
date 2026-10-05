#!/usr/bin/env python3
"""Inspect DNG layout and colour tags without decoding or changing the file."""
import argparse
from pathlib import Path
import struct

NAMES = {254: "NewSubfileType", 256: "ImageWidth", 257: "ImageLength",
         258: "BitsPerSample", 259: "Compression", 262: "PhotometricInterpretation",
         271: "Make", 272: "Model", 273: "StripOffsets", 274: "Orientation",
         277: "SamplesPerPixel",
         278: "RowsPerStrip", 279: "StripByteCounts", 50706: "DNGVersion",
         50707: "DNGBackwardVersion", 50708: "UniqueCameraModel",
         50714: "BlackLevel", 50717: "WhiteLevel", 50721: "ColorMatrix1",
         50728: "AsShotNeutral", 50778: "CalibrationIlluminant1",
         34675: "ICCProfile"}
FORMATS = {1: "B", 2: "s", 3: "H", 4: "I", 5: "II", 7: "B", 9: "i",
           10: "ii", 11: "f", 12: "d"}

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("path", type=Path)
args = parser.parse_args()
with args.path.open("rb") as image:
    header = image.read(8)
    if len(header) != 8 or header[:2] not in (b"II", b"MM"):
        raise SystemExit("Invalid TIFF header")
    endian = "<" if header[:2] == b"II" else ">"
    magic, offset = struct.unpack(endian + "HI", header[2:])
    if magic != 42:
        raise SystemExit("Only classic TIFF is supported by this probe")
    print(f"file={args.path}; bytes={args.path.stat().st_size}")
    visited = set()
    while offset:
        if offset in visited:
            raise SystemExit("Cyclic IFD chain")
        visited.add(offset)
        image.seek(offset)
        count = struct.unpack(endian + "H", image.read(2))[0]
        entries = image.read(count * 12)
        offset = struct.unpack(endian + "I", image.read(4))[0]
        tags = {}
        for index in range(count):
            entry = entries[index * 12:(index + 1) * 12]
            tag, kind, length = struct.unpack(endian + "HHI", entry[:8])
            fmt = FORMATS.get(kind)
            if fmt is None:
                print(tag, "unsupported type", kind)
                continue
            size = struct.calcsize(endian + fmt) * length
            if size <= 4:
                data = entry[8:8 + size]
            else:
                image.seek(struct.unpack(endian + "I", entry[8:])[0])
                data = image.read(size)
            if kind == 2:
                value = data.rstrip(b"\0").decode("utf-8", errors="replace")
            else:
                value = struct.unpack(endian + fmt * length, data)
                if kind in (5, 10):
                    value = tuple(value[i] / value[i + 1] if value[i + 1] else None
                                  for i in range(0, len(value), 2))
            tags[tag] = value
            print(f"{tag} {NAMES.get(tag, '')}: {str(value)[:350]}")
        if 254 not in tags:
            print("WARNING: NewSubfileType missing (Adobe SDK rejects this IFD)")
        print("pixel_ranges_in_file=", bool(tags.get(273)) and
              len(tags.get(273, ())) == len(tags.get(279, ())) and all(
            start + size <= args.path.stat().st_size
            for start, size in zip(tags.get(273, ()), tags.get(279, ()))))
