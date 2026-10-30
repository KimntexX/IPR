import unittest
import numpy as np
from study_structure_prediction import CONTROL, TRAIN, OFFSETS, evaluate, fit, samples, signal
from audit_structure_residual import compensated_variance, shape_only_variance


class PredictionContractTests(unittest.TestCase):
    def test_training_and_control_have_disjoint_source_pixels(self):
        def pixels(extent):
            lo, hi = extent
            return {(y+dy, x+dx) for y in range(lo, hi) for x in range(lo, hi)
                    for dy, dx in [(0, 0)]+OFFSETS}
        self.assertFalse(pixels(TRAIN) & pixels(CONTROL))

    def test_zero_noise_predicts_known_periodic_signal(self):
        truth = signal('diagonal', 32, .0015)
        row = evaluate(truth, np.zeros_like(truth), np.zeros_like(truth))
        self.assertGreater(row['methods']['noise_aware']['signal_gain'], .999)
        self.assertLess(row['methods']['noise_aware']['output_mse'], 1e-16)

    def test_prediction_has_no_control_or_truth_argument(self):
        rng = np.random.default_rng(152)
        source = rng.normal(size=(256, 256))
        reference = rng.normal(size=(256, 256))
        before = fit(samples(source, TRAIN), samples(reference, TRAIN), True)[0]
        source[144:, 144:] += 200
        after = fit(samples(source, TRAIN), samples(reference, TRAIN), True)[0]
        np.testing.assert_array_equal(before, after)

    def test_nonfinite_training_rejected(self):
        x = np.zeros((100, len(OFFSETS)))
        y = np.zeros(100)
        x[0, 0] = np.nan
        with self.assertRaises(ValueError):
            fit((x, y), (np.zeros_like(x), y), True)

    def test_covariance_compensation_scalar_case_and_negative_not_clamped(self):
        weights = np.array([.5])
        # N_y variance=1, N_x variance=1, covariance=.2 -> residual=.25+1-.2=1.05.
        self.assertAlmostEqual(compensated_variance(1.05, weights, np.array([[1.]]), np.array([.2])), 1.)
        self.assertLess(compensated_variance(0, weights, np.array([[1.]]), np.array([0.])), 0)

    def test_shape_formula_is_invariant_to_prior_amplitude(self):
        w = np.array([.5])
        first = shape_only_variance(1.05, w, np.array([[1.]]), np.array([.2]), 1.)
        scaled = shape_only_variance(1.05, w, np.array([[9.]]), np.array([1.8]), 9.)
        self.assertAlmostEqual(first, 1.)
        self.assertAlmostEqual(first, scaled)
        self.assertIsNone(shape_only_variance(1, w, np.array([[0.]]), np.array([0.]), 0))


if __name__ == '__main__':
    unittest.main()
