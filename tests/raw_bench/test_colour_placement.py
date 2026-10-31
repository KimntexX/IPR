"""Small deterministic checks for fixed dark-bin masks and denominators."""
import unittest
import base64
import hashlib
import tempfile
from pathlib import Path
import numpy as np
from colour_placement import choose_tiles, measure, aggregate, linear, verify_resources


class ColourPlacementTests(unittest.TestCase):
    def test_zero_black_is_not_excluded(self):
        rgb = np.zeros((128, 128, 3), dtype=np.float32)
        masks = choose_tiles(rgb)
        self.assertEqual(masks[0]['candidates'], 4)
        self.assertTrue(masks[0]['flat'])
        rows = measure(rgb, masks)
        result = aggregate(rows, rows)
        self.assertIsNone(result[0]['metrics']['chroma_HH']['median_ratio'])
        self.assertEqual(result[0]['metrics']['chroma_HH']['ratio_tiles'], 0)
        self.assertEqual(result[0]['metrics']['zero_channel_fraction']['median'], 1)

    def test_fixed_bins_do_not_migrate_when_state_changes(self):
        rgb = np.full((64, 64, 3), .001, dtype=np.float32)
        masks = choose_tiles(rgb)
        rows = measure(rgb, masks)
        changed = measure(rgb*10, masks)
        self.assertEqual(rows[0]['bin'], changed[0]['bin'])
        summary = aggregate(changed, rows)
        self.assertAlmostEqual(summary[2]['metrics']['Y']['median_ratio'], 10, places=4)

    def test_different_coordinates_refused(self):
        rgb = np.ones((64, 64, 3), dtype=np.float32)*.01
        rows = measure(rgb, choose_tiles(rgb))
        other = [dict(r) for r in rows]; other[0]['x'] += 1
        with self.assertRaises(ValueError):
            aggregate(other, rows)

    def test_linear_units_and_empty_bins(self):
        rgb = linear(np.full((64, 64, 3), 65535, dtype=np.uint16))
        np.testing.assert_allclose(rgb, 1, atol=1e-6)
        masks = choose_tiles(rgb)
        self.assertFalse(any(r['flat'] for r in masks))
        self.assertTrue(all(r['metrics'] is None for r in aggregate([], [])))

    def test_chroma_variation_is_detected(self):
        rgb = np.full((64, 64, 3), .01, dtype=np.float32)
        masks = choose_tiles(rgb)
        reference = measure(rgb, masks)
        rng = np.random.default_rng(82)
        delta = rng.normal(0, .0003, (64, 64)).astype(np.float32)
        rgb[..., 0] += delta; rgb[..., 2] -= delta
        changed = measure(rgb, masks)
        self.assertGreater(changed[0]['metrics']['chroma_HH'], reference[0]['metrics']['chroma_HH'])

    def test_nonfinite_rejected(self):
        rgb = np.zeros((64, 64, 3), dtype=np.float32); rgb[0,0,0] = np.nan
        with self.assertRaises(ValueError):
            choose_tiles(rgb)

    def test_embedded_qt_manifest_verified_without_filesystem_lookup(self):
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory)/'test.dcp'; profile.write_bytes(b'profile fixture')
            payload = b'{"schema_version":1}'
            header = dict(profile_path=str(profile), profile_sha256=hashlib.sha256(profile.read_bytes()).hexdigest(),
                          manifest_path=':/config/raw-normalization.json', manifest_kind='qt_resource',
                          manifest_base64=base64.b64encode(payload).decode('ascii'),
                          manifest_sha256=hashlib.sha256(payload).hexdigest())
            verify_resources(header)
            with self.assertRaises(ValueError):
                verify_resources(dict(header, manifest_sha256='0'*64))
            with self.assertRaises(ValueError):
                verify_resources(dict(header, manifest_kind='file'))
            with self.assertRaises(ValueError):
                verify_resources(dict(header, manifest_base64='invalid base64'))

    def test_filesystem_manifest_still_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'fixture'; path.write_bytes(b'fixture')
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            header = dict(profile_path=str(path), profile_sha256=digest,
                          manifest_path=str(path), manifest_sha256=digest, manifest_kind='file')
            verify_resources(header)
            path.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                verify_resources(header)


if __name__ == '__main__':
    unittest.main()
