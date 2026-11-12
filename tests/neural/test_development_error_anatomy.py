import unittest

import numpy as np

from development_error_anatomy import BANDS, band_fractions, noise_split, oracle_amplitude


class DevelopmentErrorAnatomyTests(unittest.TestCase):
    def test_noise_split_separates_left_noise_from_other_error(self):
        rng = np.random.default_rng(4)
        noise = rng.normal(size=(4, 64, 64))
        other = rng.normal(0, .5, size=(4, 64, 64))
        split = noise_split(.3*noise+other, noise)
        self.assertAlmostEqual(split['beta'], .3, delta=.02)
        self.assertAlmostEqual(split['noise_power']+split['rest_power'], split['total_power'], places=10)
        self.assertAlmostEqual(split['rest_power'], .25, delta=.02)
        self.assertEqual(noise_split(np.zeros((4, 8, 8)), noise[:, :8, :8])['total_power'], 0.)

    def test_band_fractions_sum_to_one_and_find_the_frequency(self):
        y, x = np.mgrid[:128, :128]
        slow = np.stack([np.cos(2*np.pi*x/64)]*4)
        fast = np.stack([np.cos(2*np.pi*x*.4)]*4)
        for planes, band in ((slow, 0), (fast, len(BANDS)-1)):
            shares = band_fractions(planes)
            self.assertAlmostEqual(sum(shares), 1., places=9)
            self.assertGreater(shares[band], .9)

    def test_oracle_amplitude_recovers_a_shrunk_correction(self):
        rng = np.random.default_rng(5)
        clean = rng.uniform(size=(4, 128, 128))
        noisy = clean+rng.normal(0, .1, size=clean.shape)
        alpha, gain = oracle_amplitude(noisy-.25*(noisy-clean), clean, noisy)
        self.assertAlmostEqual(alpha, 4., places=6)
        self.assertGreater(gain, 100)


if __name__ == '__main__':
    unittest.main()
