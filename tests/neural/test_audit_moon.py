"""Synthetic parser checks: no real image or SD card access."""
import struct
import tempfile
from pathlib import Path
import unittest

from audit_moon import metadata, tiff_fields


def tiff(tag=0x8827,kind=3,count=1,slot=None,next_ifd=0):
    slot = slot if slot is not None else struct.pack('<H',1600)+b'\0\0'
    return b'II'+struct.pack('<HI',42,8)+struct.pack('<H',1)+struct.pack('<HHI',tag,kind,count)+slot+struct.pack('<I',next_ifd)


def box(kind,payload):
    return struct.pack('>I4s',8+len(payload),kind)+payload


class MetadataTests(unittest.TestCase):
    def test_inline_iso(self):
        self.assertEqual(tiff_fields(tiff()),{'iso':1600})

    def test_bad_header(self):
        for data in (b'',b'badheader',b'II'+struct.pack('<HI',43,8)):
            with self.assertRaises(ValueError):
                tiff_fields(data)

    def test_cycle(self):
        with self.assertRaises(ValueError):
            tiff_fields(tiff(next_ifd=8))

    def test_invalid_offset(self):
        with self.assertRaises(ValueError):
            tiff_fields(tiff(kind=2,count=32,slot=struct.pack('<I',9999)))

    def test_zero_denominator(self):
        data=tiff(tag=0x829a,kind=5,slot=struct.pack('<I',26))+struct.pack('<II',1,0)
        with self.assertRaises(ValueError):
            tiff_fields(data)

    def test_fraction(self):
        data=tiff(tag=0x829a,kind=5,slot=struct.pack('<I',26))+struct.pack('<II',1,200)
        self.assertEqual(tiff_fields(data),{'exposure_s':.005})

    def test_bmff_roundtrip_and_truncation(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'synthetic.CR3'
            path.write_bytes(box(b'moov',box(b'CMT2',tiff())))
            row=metadata(path)
            self.assertEqual(row['metadata']['iso'],1600)
            self.assertFalse(row['complete_basic'])
            path.write_bytes(struct.pack('>I4s',9999,b'moov'))
            with self.assertRaises(ValueError):
                metadata(path)


if __name__=='__main__':
    unittest.main()
