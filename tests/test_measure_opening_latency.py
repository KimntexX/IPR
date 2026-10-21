import tempfile
import unittest
from pathlib import Path

from measure_opening_latency import sha, timing_records


class OpeningLatencyMeasurementTests(unittest.TestCase):
    def test_extracts_stage_timing_without_confusing_other_logs(self):
        rows = timing_records('other log\nIPR RAW timing | file=A.CR3 | stage=unpack | elapsed_ms=120 | delta_ms=80\n')
        self.assertEqual(rows, [dict(file='A.CR3', stage='unpack', elapsed_ms=120, delta_ms=80)])
        self.assertEqual(timing_records('IPR RAW normalization | white=123'), [])

    def test_sha_is_content_sensitive(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'input'
            path.write_bytes(b'abc')
            self.assertEqual(sha(path), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
            path.write_bytes(b'abd')
            self.assertNotEqual(sha(path), 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')


if __name__ == '__main__':
    unittest.main()
