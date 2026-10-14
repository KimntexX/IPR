"""Small mathematical fixtures for the independent-ROI diagnostic."""
import unittest
import numpy as np
from inspect_neutral_colour_pair import statistics
from analysis.native import linearize


class NeutralColourTests(unittest.TestCase):
    def test_gray_is_not_a_warm_subject(self):
        result = statistics(np.full((8, 8, 3), .3))
        self.assertEqual(result['warm_mask']['samples'], 0)
        self.assertEqual(result['whole_region']['samples'], 64)
        self.assertAlmostEqual(result['whole_region']['mean_Y'], float(linearize(np.array(.3))))

    def test_redder_than_orange(self):
        red = statistics(np.full((8, 8, 3), [.5, .1, .02]))
        orange = statistics(np.full((8, 8, 3), [.5, .25, .02]))
        self.assertLess(red['whole_region']['mean_linear_HSV_hue_degrees'],
                        orange['whole_region']['mean_linear_HSV_hue_degrees'])
        self.assertEqual(red['warm_mask']['samples'], 64)

    def test_uniform_linear_exposure_preserves_linear_hue(self):
        pixels = np.full((8,8,3), [.15,.025,.002])
        def encode(p):
            return np.where(p <= .0031308, 12.92*p, 1.055*p**(1/2.4)-.055)
        a = statistics(encode(pixels))['whole_region']
        b = statistics(encode(pixels*.5))['whole_region']
        self.assertAlmostEqual(a['mean_linear_HSV_hue_degrees'], b['mean_linear_HSV_hue_degrees'])
        self.assertAlmostEqual(a['mean_Y']*.5,b['mean_Y'])


if __name__ == '__main__':
    unittest.main()
