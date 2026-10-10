import unittest

import numpy as np

from analyse_lumieres import measure, setting_warnings


class LightingAnalysisTests(unittest.TestCase):
    def test_neutral_has_no_effect(self):
        base = np.linspace(.01, .8, 300).reshape(10, 10, 3)
        result = measure(base, base, 100, 'ombre')
        self.assertEqual(result['changed_pct'], 0)
        self.assertEqual(result['reversed_pct'], 0)
        for zone in ('low', 'mid', 'high'):
            self.assertEqual(result['delta_'+zone], 0)

    def test_reversed_direction_detected(self):
        base = np.full((5, 5, 3), .4)
        result = measure(base, base*.5, 100, 'blanc')
        self.assertEqual(result['reversed_pct'], 100)
        self.assertLess(result['delta_high'], 0)

    def test_contrast_not_treated_as_unidirectional(self):
        base = np.full((5, 5, 3), .4)
        self.assertNotIn('reversed_pct', measure(base, base, 100, 'contraste'))

    def test_black_zone_ratio_is_missing_not_infinite(self):
        base = np.zeros((5, 5, 3))
        self.assertIsNone(measure(base, base, 100, 'noir')['ratio_low'])

    def test_changed_optics_are_detected_even_if_target_is_correct(self):
        from unittest.mock import patch
        neutral = {'settings': {'Exposure2012': '0', 'LensProfileEnable': '0'}}
        variant = {'settings': {'Exposure2012': '1', 'LensProfileEnable': '1'}}
        with patch('analyse_lumieres.audit', return_value=[]):
            warnings = setting_warnings(variant, neutral, 'Exposure2012', 1)
        self.assertTrue(any('LensProfileEnable' in item for item in warnings))

    def test_correct_nonzero_optics_do_not_create_difference(self):
        from unittest.mock import patch
        neutral = {'settings': {'Exposure2012': '0', 'LensProfileEnable': '1'}}
        variant = {'settings': {'Exposure2012': '1', 'LensProfileEnable': '1'}}
        with patch('analyse_lumieres.audit', return_value=['LensProfileEnable=1, expected 0']):
            self.assertEqual(setting_warnings(variant, neutral, 'Exposure2012', 1), [])


if __name__ == '__main__':
    unittest.main()
