import unittest
import numpy as np
from study_luminance_ensemble import observations, candidates, fit_weights, scene_set


class LuminanceEnsembleTests(unittest.TestCase):
    def test_nine_supports_exclude_centre(self):
        plane = np.full((17, 17), .4)
        plane[8, 8] = 5
        med, spread = observations(plane)
        self.assertEqual(med.shape, (9, 17, 17))
        np.testing.assert_array_equal(med[:, 8, 8], .4)
        np.testing.assert_array_equal(spread[:, 8, 8], 0)

    def test_distinct_half_lines(self):
        y, x = np.mgrid[:17, :17]
        med, _ = observations(x + 10 * y)
        np.testing.assert_array_equal(med[5:, 8, 8], [68, 108, 86, 90])

    def test_constant_hdr_and_immutable_input(self):
        for value in (-.2, 0, .4, 2):
            plane = np.full((17, 17), value)
            outputs, sigma = candidates(plane)
            np.testing.assert_allclose(outputs, value, atol=1e-14)
            np.testing.assert_array_equal(plane, value)
            self.assertEqual(sigma, 0)

    def test_rotation_symmetry(self):
        plane = np.random.default_rng(8).normal(.4, .03, (20, 20))
        outputs, _ = candidates(plane)
        rotated, _ = candidates(np.rot90(plane))
        np.testing.assert_allclose(rotated, np.rot90(outputs, axes=(1, 2)), atol=1e-12)

    def test_convex_weights(self):
        weights = fit_weights(np.diag([4., 1.]))
        np.testing.assert_allclose(weights, [.2, .8], atol=1e-6)
        self.assertTrue((weights >= 0).all())
        self.assertAlmostEqual(weights.sum(), 1)

    def test_identical_errors_and_split(self):
        weights = fit_weights(np.ones((3, 3)))
        self.assertAlmostEqual(weights @ np.ones((3, 3)) @ weights, 1)
        self.assertFalse(set(scene_set("calibration")) & set(scene_set("validation")))

    def test_invalid_inputs(self):
        with self.assertRaises(ValueError):
            observations(np.ones((6, 6)))
        with self.assertRaises(ValueError):
            observations(np.full((7, 7), np.nan))
        with self.assertRaises(ValueError):
            fit_weights(np.array([[1., 2.], [2., 1.]]))


if __name__ == "__main__":
    unittest.main()
