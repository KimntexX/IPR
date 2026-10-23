import struct
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
import numpy as np
from audit_noise_pairs import tiff_fields, pair_stats, matching, input_paths


class PairAuditTests(unittest.TestCase):
    def test_tiff_inline_iso(self):
        data = b'II' + struct.pack('<HI', 42, 8) + struct.pack('<H', 1)
        data += struct.pack('<HHII', 0x8827, 3, 1, 1600) + bytes(4)
        self.assertEqual(tiff_fields(data)['iso'], 1600)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            tiff_fields(b'II')

    def test_white_variance(self):
        rng = np.random.default_rng(151)
        result = pair_stats(rng.normal(2048, 3, (128, 128)), rng.normal(2048, 3, (128, 128)))
        self.assertLess(abs(result['noise_variance_dn2'] - 9), .4)
        self.assertEqual(result['status'], 'candidate_stable')

    def test_large_drift(self):
        result = pair_stats(np.ones((128, 128))*100, np.ones((128, 128))*110)
        self.assertEqual(result['status'], 'reject_drift_or_structure')

    def test_missing_metadata_is_not_match(self):
        self.assertFalse(matching({}, {}))

    def test_shared_blacks_are_assigned_by_metadata_iso(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            series = root / 'protocole' / 'iso-16000'
            blacks = root / 'protocole' / 'blacks'
            series.mkdir(parents=True)
            blacks.mkdir()
            (series / 'light.CR3').write_bytes(b'16000')
            (blacks / 'dark-a.CR3').write_bytes(b'16000')
            (blacks / 'dark-b.CR3').write_bytes(b'16000')
            (blacks / 'other.CR3').write_bytes(b'51200')
            with patch('audit_noise_pairs.metadata', side_effect=lambda data: {'iso': int(data)}):
                paths = input_paths(root, series)
            self.assertEqual([(p.name, dark) for p, dark in paths],
                             [('light.CR3', False), ('dark-a.CR3', True), ('dark-b.CR3', True)])

    def test_misplaced_light_is_grouped_without_moving(self):
        with TemporaryDirectory() as folder:
            root = Path(folder)
            wrong = root / 'protocole' / 'iso-3200'
            target = root / 'protocole' / 'iso-6400'
            wrong.mkdir(parents=True)
            target.mkdir()
            source = wrong / 'misplaced.CR3'
            source.write_bytes(b'6400')
            with patch('audit_noise_pairs.metadata', side_effect=lambda data: {'iso': int(data)}):
                self.assertEqual(input_paths(root, target), [(source, False)])
                self.assertEqual(input_paths(root, wrong), [])
            self.assertTrue(source.exists())


if __name__ == '__main__':
    unittest.main()
