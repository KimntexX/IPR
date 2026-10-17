import struct
import tempfile
from pathlib import Path
import unittest
from inspect_saved_raw_state import Reader,inspect


class SavedStateTests(unittest.TestCase):
    def test_QString_and_boolean(self):
        reader=Reader(struct.pack('>I',4)+'AB'.encode('utf-16-be')+b'\x01')
        self.assertEqual(reader.string(),'AB');self.assertTrue(reader.boolean())

    def test_truncation_and_bad_boolean_refused(self):
        with self.assertRaises(ValueError):Reader(b'\x00').number('I')
        with self.assertRaises(ValueError):Reader(b'\x02').boolean()
        with self.assertRaises(ValueError):Reader(struct.pack('>I',3)+b'abc').string()

    def test_current_layout_and_optical_flags(self):
        def string(value):
            encoded=value.encode('utf-16-be');return struct.pack('>I',len(encoded))+encoded
        with tempfile.TemporaryDirectory() as directory:
            raw=Path(directory)/'test.CR3';raw.write_bytes(b'raw')
            data=struct.pack('>IHqqdiiB',0x49505231,6,3,raw.stat().st_mtime_ns//1000000,0,0,0,0)
            values=[0]*15;values[4]=5200
            data+=struct.pack('>15idi',*values,1.,0)
            data+=struct.pack('>30iB4ii',*([0]*30),0,0,0,0,0,0)
            data+=string('')+b'\x00\x00\x00'
            for value in ('neutral.dcp','a'*64,':/config/raw-normalization.json','b'*64):data+=string(value)
            data+=b'\x01'+string('engine')+string('lens.lcp')+string('c'*64)+b'\x01\x01\x01'
            sidecar=Path(str(raw)+'.ipr');sidecar.write_bytes(data)
            result=inspect(sidecar)
            self.assertTrue(result['source_signature_matches'])
            self.assertEqual(result['settings']['temperature'],5200)
            self.assertEqual(result['settings']['vibrance'],0)
            self.assertTrue(result['raw']['normalizationEnabled'])
            self.assertTrue(result['raw']['lensCorrectionEnabled'])
            self.assertTrue(result['raw']['chromaticAberrationEnabled'])


if __name__=='__main__':unittest.main()
