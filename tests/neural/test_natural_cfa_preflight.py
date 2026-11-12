import unittest
import numpy as np
from natural_cfa_preflight import normalized_patch


class NaturalCfaTests(unittest.TestCase):
    def test_canonicalization_all_phases_preserves_signed_sensor_values(self):
        pattern = np.array([[0, 1], [3, 2]])
        values = np.array([10, 60, 90, 80])
        for y in (False, True):
            for x in (False, True):
                p = pattern[::-1 if y else 1, ::-1 if x else 1]
                image = np.tile(values[p], (8, 8))
                result = normalized_patch(image, p, [20]*4, 100)
                self.assertEqual(result.shape, (4, 8, 8))
                np.testing.assert_allclose(result[0], -.125)
                np.testing.assert_allclose(result[3], .875)
                self.assertTrue(np.isfinite(result).all())

    def test_black_is_subtracted_per_physical_colour_before_reflection(self):
        pattern = np.array([[2, 3], [1, 0]])
        black = np.array([10, 20, 30, 40])
        tile = np.tile(black[pattern], (4, 4))
        np.testing.assert_array_equal(normalized_patch(tile, pattern, black, 100), 0)

    def test_invalid_white_or_pattern_refused(self):
        with self.assertRaises(ValueError):
            normalized_patch(np.ones((8, 8)), [[0, 1], [3, 2]], [20]*4, 20)
        with self.assertRaises(ValueError):
            normalized_patch(np.ones((8, 8)), [[0, 1], [1, 2]], [20]*4, 100)


if __name__ == '__main__':
    unittest.main()
