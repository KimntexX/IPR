import unittest
import numpy as np
from study_local_luminance import local_features, fuse, calibrate, MODELS, REGIMES, holdout_scenes
from study_luminance_ensemble import scene_set


class LocalLuminanceTests(unittest.TestCase):
    def test_constant_hdr_unchanged(self):
        weights = np.full((len(REGIMES), len(MODELS)), 1 / len(MODELS))
        for value in (-.2, 0, .2, 2):
            original = np.full((17, 17), value)
            outputs, regimes, _ = local_features(original)
            for abstain in (False, True):
                np.testing.assert_allclose(fuse(outputs, regimes, weights, abstain), value, atol=1e-14)
            np.testing.assert_array_equal(original, value)

    def test_abstention_exact_and_convex(self):
        outputs = np.random.default_rng(10).normal(size=(len(MODELS), 15, 15))
        regimes = np.full((15, 15), 3)
        weights = np.full((len(REGIMES), len(MODELS)), 1 / len(MODELS))
        np.testing.assert_array_equal(fuse(outputs, regimes, weights), outputs[0])
        result = fuse(outputs, regimes, weights, False)
        self.assertTrue((result >= outputs.min(axis=0)).all())
        self.assertTrue((result <= outputs.max(axis=0)).all())

    def test_supported_diagonal(self):
        y, x = np.mgrid[:21, :21]
        plane = np.where(x == y, .8, .1)
        outputs, regimes, _ = local_features(plane)
        self.assertEqual(regimes[10, 10], 2)
        self.assertAlmostEqual(outputs[-1, 10, 10], .8)

    def test_rotation_equivariance(self):
        plane = np.random.default_rng(5).normal(.4, .03, (20, 20))
        outputs, regimes, _ = local_features(plane)
        rotated, labels, _ = local_features(np.rot90(plane))
        np.testing.assert_allclose(rotated, np.rot90(outputs, axes=(1, 2)), atol=1e-12)
        np.testing.assert_array_equal(labels, np.rot90(regimes))

    def test_holdout_not_previous_scene(self):
        old = set(scene_set("calibration")) | set(scene_set("validation"))
        self.assertFalse(old & set(holdout_scenes()))

    def test_invalid_fusion(self):
        outputs = np.ones((len(MODELS), 10, 10))
        labels = np.zeros((10, 10), dtype=np.int8)
        with self.assertRaises(ValueError):
            fuse(outputs, labels, np.ones((len(REGIMES), len(MODELS))))

    def test_sparse_calibration_abstains(self):
        plane = np.full((17, 17), .4)
        weights, _, _, counts = calibrate([{"noisy": plane, "clean": plane}])
        self.assertEqual(counts.sum(), 25)
        np.testing.assert_array_equal(weights[:, 0], 1)
        np.testing.assert_array_equal(weights[:, 1:], 0)


if __name__ == "__main__":
    unittest.main()
