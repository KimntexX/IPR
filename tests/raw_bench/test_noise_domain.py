"""Small deterministic controls; no RAW/image generation or canonical proof writes."""
import math
import unittest
from study_noise_domain import algebraic_inverse, encode, naive_vst, positive_gaussian_mean


class NoiseDomainTests(unittest.TestCase):
    def test_centered_clipped_normal(self):
        self.assertAlmostEqual(positive_gaussian_mean(0., .01), .01/math.sqrt(2*math.pi))

    def test_positive_clipping_monotone_in_sigma(self):
        self.assertGreater(positive_gaussian_mean(.001, .01), positive_gaussian_mean(.001, .002))

    def test_invalid_sigma(self):
        with self.assertRaises(ValueError):
            positive_gaussian_mean(0., 0.)

    def test_encode_black_and_white(self):
        self.assertEqual(encode(-1.), 0.)
        self.assertEqual(encode(0.), 0.)
        self.assertAlmostEqual(encode(1.), 1.)

    def test_vst_round_trip_inside_domain(self):
        for x in (0., .01, 1.):
            self.assertAlmostEqual(algebraic_inverse(naive_vst(x, .001, .00001), .001, .00001), x)

    def test_vst_negative_tail_is_not_invertible(self):
        self.assertEqual(naive_vst(-.02, .001, .00001), naive_vst(-.03, .001, .00001))
        self.assertNotEqual(algebraic_inverse(naive_vst(-.02, .001, .00001), .001, .00001), -.02)


if __name__ == '__main__':
    unittest.main()
