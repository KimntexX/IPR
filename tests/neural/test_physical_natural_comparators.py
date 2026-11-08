import unittest

import numpy as np

from physical_natural_comparators import anscombe, inverse_anscombe, score


class ComparatorTests(unittest.TestCase):
    def test_anscombe_stabilises_poisson_gaussian_variance(self):
        rng = np.random.default_rng(4)
        a, b = 6.6/12303, 160/12303**2
        for level in (.005, .05, .4):
            y = a*rng.poisson(level/a, 200000)+rng.normal(0, b**.5, 200000)
            self.assertAlmostEqual(float(anscombe(y, a, b).std()), 1., delta=.05)

    def test_unbiased_inverse_recovers_the_mean_of_transformed_noise(self):
        rng = np.random.default_rng(7)
        a, b = 32.5/12303, 3480/12303**2
        for level in (.01, .1, .5):
            y = a*rng.poisson(level/a, 400000)+rng.normal(0, b**.5, 400000)
            recovered = float(inverse_anscombe(np.array([anscombe(y, a, b).mean()]), a, b)[0])
            self.assertAlmostEqual(recovered/level, 1., delta=.03)

    def test_score_uses_the_inner_region_and_reports_gain(self):
        clean = np.zeros((4, 96, 96))
        noisy = np.full((4, 96, 96), .02)
        estimate = np.full((4, 96, 96), .01)
        estimate[:, :32] = 5.
        result = score(estimate, clean, noisy)
        self.assertAlmostEqual(result['psnr_gain_db'], 10*np.log10(4), places=6)
        self.assertEqual(len(result['mean_error_by_plane']), 4)


if __name__ == '__main__':
    unittest.main()
