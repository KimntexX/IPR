import unittest
import numpy as np
from study_noise_planes import noise_map, residual_diagnostic


class NoisePlanesTests(unittest.TestCase):
    def test_constant_plane(self):
        sigma, confidence, rows = noise_map(np.full((48, 51), -.2))
        self.assertTrue(np.isfinite(sigma).all())
        self.assertLess(sigma.max(), 1e-12)
        self.assertEqual(confidence.max(), 0)
        self.assertEqual(len(rows), 6)

    def test_white_noise(self):
        p = np.random.default_rng(818).normal(.4, .025, (64, 64))
        sigma, confidence, _ = noise_map(p)
        self.assertTrue(.018 < sigma.mean() < .032)
        self.assertTrue(((confidence >= 0) & (confidence <= 1)).all())

    def test_reject_nonfinite(self):
        for p in (np.zeros((8, 8)), np.full((32, 32), np.nan)):
            with self.assertRaises(ValueError):
                noise_map(p)

    def test_residual_excludes_center(self):
        p = np.zeros((40, 40))
        p[20, 20] = 1
        flagged, eligible, z = residual_diagnostic(p, np.full_like(p, .01), np.ones_like(p))
        self.assertTrue(flagged[20, 20])
        self.assertAlmostEqual(z[20, 20], 1 / (.01 * np.sqrt(9 / 8)))
        self.assertFalse(eligible[0].any())

    def test_disagreement_abstains(self):
        p = np.ones((40, 40))
        flagged, eligible, _ = residual_diagnostic(p, np.ones_like(p), np.zeros_like(p))
        self.assertFalse(flagged.any())
        self.assertFalse(eligible.any())


if __name__ == '__main__':
    unittest.main()
