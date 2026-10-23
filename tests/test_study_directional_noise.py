import unittest
import numpy as np
from study_directional_noise import five_windows


class FiveWindowTests(unittest.TestCase):
    def test_constant_and_hdr(self):
        for value in (-.2, 0, .1, 2.0):
            plane = np.full((17, 19), value)
            for gated in (False, True):
                np.testing.assert_array_equal(five_windows(plane, .01, gated)[0], plane)

    def test_isolated_outliers_and_input_immutable(self):
        for value, polarity in ((.9, 1), (-.1, -1)):
            plane = np.full((17, 19), .3)
            plane[8, 9] = value
            original = plane.copy()
            result, sign, mask = five_windows(plane, .01)
            self.assertTrue(mask[8, 9])
            self.assertEqual(sign[8, 9], polarity)
            self.assertEqual(result[8, 9], .3)
            np.testing.assert_array_equal(plane, original)

    def test_rotation_symmetry(self):
        plane = np.random.default_rng(3).normal(.4, .03, (21, 21))
        for gated in (False, True):
            result = five_windows(plane, .02, gated)[0]
            np.testing.assert_allclose(five_windows(np.rot90(plane), .02, gated)[0], np.rot90(result))

    def test_invalid_input(self):
        for plane in (np.ones((2, 2)), np.ones((7, 7, 3)), np.full((7, 7), np.nan)):
            with self.assertRaises(ValueError):
                five_windows(plane, .01)
        with self.assertRaises(ValueError):
            five_windows(np.ones((7, 7)), -1)


if __name__ == "__main__":
    unittest.main()
