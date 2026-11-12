from pathlib import Path
import struct
import tempfile
import unittest
from canon_capture_mode import quality_fields,read_mode,label,CANON_UUID


def sample():
    header=b'II'+struct.pack('<HI',42,8)
    root=struct.pack('<H',1)+struct.pack('<HHII',1,3,8,26)+struct.pack('<I',0)
    return header+root+struct.pack('<8h',16,0,0,7,0,0,0,0)


class CaptureModeTests(unittest.TestCase):
    def test_documented_quality_index(self):
        self.assertEqual(quality_fields(sample()),dict(CameraSettingsQuality=7))
        self.assertEqual(label(dict(CameraSettingsQuality=7)),'CRAW')
        self.assertEqual(label(dict(CameraSettingsQuality=4,RawJpgQuality=7)),'unknown_or_conflicting')

    def test_canonical_nested_container(self):
        def box(kind,data):return struct.pack('>I4s',8+len(data),kind)+data
        data=box(b'moov',box(b'uuid',CANON_UUID+box(b'CMT3',sample())))
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'test.CR3';path.write_bytes(data)
            self.assertEqual(read_mode(path)['mode'],'CRAW')

    def test_invalid_payload_offset_refused(self):
        data=bytearray(sample());struct.pack_into('<I',data,18,99999)
        with self.assertRaises(ValueError):quality_fields(data)


if __name__=='__main__':unittest.main()
