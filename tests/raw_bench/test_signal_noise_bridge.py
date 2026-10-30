import math
import unittest
from audit_signal_noise_bridge import PHASES, normalize_coefficients


class SignalNoiseBridgeTests(unittest.TestCase):
    def test_affine_transform_preserves_variance_and_black_origin(self):
        slope,b,reference,runtime,gain,divisor = 3.5,40.,2048.4,2048.,1.9,14335.
        a,c = normalize_coefficients(slope,b,reference,runtime,gain,divisor)
        for signal in (0,50,1000):
            x = gain*(signal+reference-runtime)/divisor
            expected = (b+slope*signal)*gain**2/divisor**2
            self.assertAlmostEqual(a*x+c,expected,places=16)

    def test_slope_scales_with_gain_and_intercept_with_gain_squared(self):
        first = normalize_coefficients(3,40,2048,2048,1,10000)
        second = normalize_coefficients(3,40,2048,2048,2,10000)
        self.assertAlmostEqual(second[0],2*first[0])
        self.assertAlmostEqual(second[1],4*first[1])

    def test_negative_normalized_mean_is_not_clamped(self):
        reference,runtime,gain,divisor = 2047.8,2048.,1.9,14335.
        a,b = normalize_coefficients(3.5,40,reference,runtime,gain,divisor)
        x = gain*(reference-runtime)/divisor
        self.assertLess(x,0)
        self.assertAlmostEqual(a*x+b,40*gain**2/divisor**2,places=16)

    def test_phase_order_is_not_colour_index_order(self):
        self.assertEqual(PHASES['G2'],(1,0,3))
        self.assertEqual(PHASES['B'],(1,1,2))

    def test_invalid_context_fails(self):
        for gain,divisor in ((0,1),(1,0),(math.nan,1),(1,math.inf)):
            with self.assertRaises(ValueError):
                normalize_coefficients(3,40,2048,2048,gain,divisor)
        with self.assertRaises(ValueError):
            normalize_coefficients(-1,40,2048,2048,1,10000)


if __name__ == '__main__':
    unittest.main()
