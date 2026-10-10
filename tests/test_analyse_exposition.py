"""Synthetic checks for EV measurements; independent of rendering quality."""
import unittest

import numpy as np

from analyse_exposition import audit, response


def encode(rgb):
    return np.where(rgb <= .0031308, 12.92 * rgb, 1.055 * rgb ** (1/2.4) - .055)


class ExposureAnalysisTests(unittest.TestCase):
    def test_neutral_supports_new_generic_lightroom_name(self):
        from pathlib import Path
        from unittest.mock import patch
        from analyse_exposition import neutral_file
        with patch.object(Path, 'exists', return_value=False):
            self.assertEqual(neutral_file(Path('base'), 'X', 'LR'), Path('base/neutre/X.tif'))
        with patch.object(Path, 'exists', return_value=True):
            self.assertEqual(neutral_file(Path('base'), 'X', 'LR'),
                             Path('base/neutre/X-LR-CameraNeutral-AsShot.tif'))

    def test_exact_linear_ev(self):
        base = np.linspace(.01, .4, 300).reshape(10, 10, 3)
        for ev in (-1, 1):
            measured = response(encode(base), encode(base * 2**ev), ev)
            self.assertAlmostEqual(measured['median_ev_unclipped'], ev, places=10)
            self.assertLess(measured['mae_linear_expected_unclipped'], 1e-12)
            self.assertEqual(measured['direction_reversed_pct'], 0)

    def test_clipping_excluded_from_gain(self):
        base = np.array([[[.1]*3, [.8]*3]])
        measured = response(encode(base), encode(np.minimum(2*base, 1)), 1)
        self.assertEqual(measured['unclipped_samples'], 1)
        self.assertEqual(measured['clipped_channel_pct'], 50)
        self.assertAlmostEqual(measured['median_ratio_unclipped'], 2)

    def test_missing_usable_pixels_are_not_zero_error(self):
        measured = response(np.zeros((2, 2, 3)), np.zeros((2, 2, 3)) + .01, 1)
        self.assertIsNone(measured['median_ev_unclipped'])
        self.assertIsNone(measured['mae_linear_expected_unclipped'])

    def test_geometry_mismatch_rejected(self):
        with self.assertRaises(ValueError):
            response(np.zeros((2, 2, 3)), np.zeros((3, 2, 3)), 1)

    def test_wrong_profile_and_ev_audited(self):
        warnings = audit({'icc': 'sRGB', 'bits': [16]*3, 'active_mixer': {},
            'settings': {'CameraProfile': 'Adobe Standard', 'Exposure2012': '0'}}, 1)
        self.assertTrue(any('CameraProfile' in warning for warning in warnings))
        self.assertTrue(any('Exposure2012' in warning for warning in warnings))


if __name__ == '__main__':
    unittest.main()
