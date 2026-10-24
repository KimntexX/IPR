import struct
import tempfile
import unittest
from pathlib import Path

from inventory_noise_sources import cr2_metadata, cr3_metadata


class InventoryTests(unittest.TestCase):
    def test_cr3_reads_metadata_and_skips_large_media_payload(self):
        tiff = b'II' + struct.pack('<HIH', 42, 8, 1) + struct.pack('<HHII', 0x8827, 3, 1, 1250) + b'\0'*4
        cmt = struct.pack('>I4s', 8 + len(tiff), b'CMT2') + tiff
        blob = struct.pack('>I4s', 8 + len(cmt), b'moov') + cmt + struct.pack('>I4s', 1032, b'mdat') + b'\0'*1024
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'test.CR3'
            path.write_bytes(blob)
            self.assertEqual(cr3_metadata(path)['iso'], 1250)
            self.assertEqual(path.read_bytes(), blob)

    def test_cr2_little_endian_iso(self):
        blob = b'II' + struct.pack('<HIH', 42, 8, 1) + struct.pack('<HHII', 0x8827, 3, 1, 800) + b'\0'*4
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'test.CR2'
            path.write_bytes(blob)
            self.assertEqual(cr2_metadata(path)['iso'], 800)

    def test_invalid_headers_and_box_extents_are_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'invalid.CR2'
            path.write_bytes(b'not raw image')
            with self.assertRaises(ValueError):
                cr2_metadata(path)
            path.write_bytes(struct.pack('>I4s', 100, b'moov'))
            with self.assertRaises(ValueError):
                cr3_metadata(path)


if __name__ == '__main__':
    unittest.main()
