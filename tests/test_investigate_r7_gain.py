import importlib.util
import unittest
import numpy as np

from investigate_r7_gain import encode, fit_tiles, gain, register, score, summarize_tiles


class R7GainStudyTests(unittest.TestCase):
    def test_known_linear_gain_and_tonal_summary(self):
        y, x = np.indices((192, 192))
        a = np.repeat((.03+.2*x/192)[..., None], 3, axis=-1).astype(np.float32)
        b = a*1.04
        tiles, valid = fit_tiles(a, b, np.ones((192, 192), bool))
        self.assertAlmostEqual(gain(tiles), 1/1.04, places=6)
        summary = summarize_tiles(tiles)
        self.assertAlmostEqual(summary['channel_gains'][0], 1/1.04, places=6)
        before, after = score(a, b, valid, 1), score(a, b, valid, gain(tiles))
        self.assertLess(after['MAE_Y'], before['MAE_Y']*.001)
        self.assertLess(after['median_DeltaE00'], .0001)

    def test_excludes_black_clipped_and_outside_overlap(self):
        a = np.full((192, 192, 3), .2, np.float32)
        b = a.copy()
        b[:24] = 1
        b[24:48] = 0
        support = np.ones((192, 192), bool)
        support[48:72] = False
        tiles, valid = fit_tiles(a, b, support)
        self.assertFalse(valid[:72].any())
        self.assertTrue(all(t['y'] >= 72 for t in tiles))
        self.assertAlmostEqual(gain(tiles), 1)

    def test_requires_spatial_coverage(self):
        a = np.full((48, 48, 3), .2, np.float32)
        with self.assertRaisesRegex(ValueError, 'Insufficient'):
            fit_tiles(a, a, np.ones((48, 48), bool))

    @unittest.skipUnless(importlib.util.find_spec('cv2'), 'OpenCV (cv2) is optional outside manual studies')
    def test_registration_recovers_translation_not_photometric_gain(self):
        import cv2
        random = np.random.default_rng(15)
        a = cv2.GaussianBlur(random.uniform(.02, .5, (300, 330, 3)).astype(np.float32), (0, 0), 2)
        b = cv2.warpAffine(a*1.03, np.float32([[1, 0, 3], [0, 1, -2]]), (330, 300))
        aligned, valid, proof = register(a, b)
        transform = np.array(proof['affine_LR_to_IPR_quarter_size'])
        self.assertAlmostEqual(transform[0, 2], 3, delta=.15)
        self.assertAlmostEqual(transform[1, 2], -2, delta=.15)
        self.assertGreater(proof['edge_correlation'], .99)
        self.assertLess(np.abs(aligned[valid]/1.03-a[valid]).mean(), .001)

    def test_encode_round_trip(self):
        from analysis.native import linearize
        values = np.array([0, .0001, .0031308, .18, 1])
        np.testing.assert_allclose(linearize(encode(values)), values, atol=1e-8)


if __name__ == '__main__':
    unittest.main()
