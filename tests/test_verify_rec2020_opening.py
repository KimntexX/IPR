import unittest
import numpy as np
from verify_rec2020_opening import comparison


class Rec2020OpeningTests(unittest.TestCase):
    def test_identical_native_pixels(self):
        pixels = np.array([[[0, 8000, 40000], [65535, 65535, 65535]]], dtype=np.uint16)
        result = comparison(pixels, pixels.copy())
        self.assertTrue(result['neutral_gate_pass'])
        self.assertEqual(result['max_16bit_codes'], 0)

    def test_large_code_difference_is_not_hidden_by_mean_delta_e(self):
        base = np.full((10, 10, 3), 32000, dtype=np.uint16)
        changed = base.copy()
        changed[0, 0, 0] += 50
        result = comparison(base, changed)
        self.assertFalse(result['neutral_gate_pass'])
        self.assertEqual(result['max_16bit_codes'], 50)

    def test_incompatible_grid_rejected(self):
        with self.assertRaises(ValueError):
            comparison(np.zeros((1,1,3), dtype=np.uint16), np.zeros((2,1,3), dtype=np.uint16))


if __name__ == '__main__':
    unittest.main()
