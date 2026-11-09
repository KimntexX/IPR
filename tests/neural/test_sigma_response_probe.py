import unittest

import numpy as np

from sigma_response_probe import fit_alpha


class SigmaResponseProbeTests(unittest.TestCase):
    def test_fit_alpha_reports_the_missing_amplitude_and_both_gains(self):
        noise = np.random.default_rng(6).normal(size=(4, 64, 64))
        alpha, gain, oracle = fit_alpha(noise/3, noise)
        self.assertAlmostEqual(alpha, 3., places=9)
        self.assertAlmostEqual(gain, 10*np.log10(1/(2/3)**2), places=9)
        self.assertGreater(oracle, 100)


if __name__ == '__main__':
    unittest.main()
