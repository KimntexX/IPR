import unittest
import numpy as np
from study_noise_risk import predict, margin, calibrate, apply_policy, source_scenes, METHODS, POLICIES


class NoiseRiskTests(unittest.TestCase):
    def test_constant_hdr(self):
        for value in (-.2, 0, .2, 2):
            plane = np.full((17, 17), value)
            outputs, bins, _ = predict(plane)
            np.testing.assert_allclose(outputs, value, atol=1e-14)
            np.testing.assert_array_equal(plane, value)
            self.assertTrue(np.isin(bins, np.arange(12)).all())

    def test_action_identity_exact(self):
        outputs = np.random.default_rng(8).normal(size=(len(METHODS), 12, 12))
        labels = np.zeros((12, 12), dtype=int)
        np.testing.assert_array_equal(apply_policy(outputs, labels, np.zeros(12, dtype=int)), outputs[0])

    def test_selected_model_exact(self):
        outputs = np.stack([np.full((12, 12), i) for i in range(len(METHODS))])
        labels = np.zeros((12, 12), dtype=int)
        np.testing.assert_array_equal(apply_policy(outputs, labels, np.full(12, 2)), 2)

    def test_sparse_blocks_abstain(self):
        plane = np.full((17, 17), .2)
        predictions, bins, sigma = predict(plane)
        actions, frequency, _ = calibrate([dict(scene="one", noisy=plane, clean=plane,
                                               predictions=predictions, bins=bins, sigma=sigma,
                                               impulse=np.zeros((17, 17), dtype=bool), noise="clean")])
        for policy in POLICIES:
            np.testing.assert_array_equal(actions[policy], 0)
        self.assertTrue(((frequency > 0) & (frequency < 1)).all())
        self.assertIsNone(margin([-.1] * 5))

    def test_block_margin(self):
        self.assertAlmostEqual(margin([-.1] * 6), -.1)
        self.assertGreater(margin([-.1, -.1, -.1, -.1, -.1, 2]), 0)

    def test_holdout_disjoint(self):
        self.assertFalse(set(source_scenes("calibration")) & set(source_scenes("holdout")))

    def test_identical_input_cannot_identify_truth(self):
        observed = np.full((17, 17), .2)
        observed[8, 8] = .9
        outputs, _, _ = predict(observed)
        for value in outputs[:, 8, 8]:
            self.assertGreaterEqual(max(abs(value - .2), abs(value - .9)), .35 - 1e-14)

    def test_invalid_action(self):
        with self.assertRaises(ValueError):
            apply_policy(np.ones((4, 10, 10)), np.zeros((10, 10), dtype=int), np.full(12, 7))


if __name__ == "__main__":
    unittest.main()
