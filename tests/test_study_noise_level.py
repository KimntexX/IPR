import unittest
import numpy as np
from study_noise_level import production_port, upper_median, prototypes, scenes, block_counts


class NoiseLevelTests(unittest.TestCase):
    def test_upper_not_averaged_median(self):
        self.assertEqual(upper_median(np.array([0., 1., 4., 5.])), 4)

    def test_constant_hdr(self):
        for value in (-.1, 0, .4, 2.):
            p = np.full((24, 24), value, dtype=np.float32)
            self.assertEqual(production_port(p), 0)
            results, _ = prototypes(p)
            for estimate in results.values():
                self.assertLess(abs(estimate), 1e-10)

    def test_black_border_production(self):
        p = np.zeros((96, 96), dtype=np.float32)
        p[24:72, 24:72] = np.random.default_rng(1).normal(.4, .025, (48, 48))
        self.assertGreater(production_port(p), .02)
        self.assertLess(production_port(p), .03)

    def test_unit_white(self):
        p = np.random.default_rng(2).normal(size=(192, 192)).astype(np.float32)
        self.assertAlmostEqual(production_port(p), 1, delta=.06)
        estimates, _ = prototypes(p)
        self.assertAlmostEqual(estimates["laplacian_mad"], 1, delta=.06)
        self.assertAlmostEqual(estimates["weak_dct"], 1, delta=.1)

    def test_invalid(self):
        for plane in (np.ones((4, 4)), np.full((16, 16), np.nan)):
            with self.assertRaises(ValueError):
                prototypes(plane)

    def test_texture_control_present(self):
        self.assertIn("stochastic_clean_texture", scenes())
        self.assertIn("black_border", scenes())

    def test_constant_exclusion_on_clean_edge(self):
        plane = scenes()["slanted_edge"].astype(np.float32)
        count = block_counts(plane)
        self.assertGreater(count["constant_blocks"], count["retained_blocks"])
        self.assertGreater(production_port(plane), .4)


if __name__ == "__main__":
    unittest.main()
