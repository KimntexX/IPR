import math
import unittest

import numpy as np

from real_noise_bench import phase_shift
from real_noise_evaluation import CROP, DIVISOR, crop_window, fit_mapping, noise_law, same_burst_score, score_method


def bank():
    fits = []
    for iso, slope in ((6400, 12.), (12800, None), (16000, 30.)):
        for plane in range(4):
            fits.append(dict(iso=iso, plane=plane, fit={'shot_slope_dn': slope}, reference_dark_variance_dn2=float(iso)))
    return dict(fits=fits)


class RealNoiseEvaluationTests(unittest.TestCase):
    def test_noise_law_keeps_measured_values_and_interpolates_a_missing_slope(self):
        rows, provenance = noise_law(bank(), 6400)
        self.assertEqual(rows[0], (12., 6400.))
        self.assertEqual(provenance, 'measured')
        rows, provenance = noise_law(bank(), 12800)
        t = math.log(2)/math.log(2.5)
        self.assertAlmostEqual(rows[2][0], math.exp((1-t)*math.log(12)+t*math.log(30)), places=9)
        self.assertEqual(rows[2][1], 12800.)
        self.assertIn('interpolated', provenance)

    def test_crop_window_follows_the_phase_correlation_convention(self):
        rng = np.random.default_rng(7)
        image = rng.normal(size=(1200, 1700)).cumsum(axis=0).cumsum(axis=1)
        moving = np.roll(image, (-3, 11), axis=(0, 1))
        shift, _ = phase_shift(image[88:1112, 82:1618], moving[88:1112, 82:1618])
        ref_crop, mov_crop = crop_window(image.shape, shift)
        np.testing.assert_array_equal(image[ref_crop], moving[mov_crop])
        self.assertEqual(image[ref_crop].shape, CROP)
        with self.assertRaises(ValueError):
            crop_window(image.shape, (500, 0))

    def test_mapping_and_score_are_shared_by_every_method(self):
        rng = np.random.default_rng(8)
        reference = rng.uniform(.1, .5, size=(4, 128, 160)).cumsum(axis=-1)/40
        frame_clean = (reference-.01)/1.25
        noisy = frame_clean+rng.normal(0, .02, size=reference.shape)
        valid = np.ones(reference.shape, bool)
        mapping = fit_mapping(noisy, reference, valid)
        for gain, offset in mapping:
            self.assertAlmostEqual(gain, 1.25, delta=.02)
            self.assertAlmostEqual(offset, .01, delta=.005)
        perfect = score_method(frame_clean, noisy, reference, valid, [(1.25, .01)]*4)
        self.assertGreater(perfect['psnr_gain_db'], 100)
        self.assertAlmostEqual(perfect['oracle_correction_alpha'], 1., places=9)
        halfway = score_method(noisy-.5*(noisy-frame_clean), noisy, reference, valid, [(1.25, .01)]*4)
        self.assertAlmostEqual(halfway['psnr_gain_db'], 10*math.log10(4), delta=1e-6)
        self.assertAlmostEqual(halfway['oracle_correction_alpha'], 2., places=6)

    def test_same_burst_score_removes_the_reference_noise(self):
        rng = np.random.default_rng(9)
        truth = np.tile(np.linspace(.05, .4, 256), (4, 192, 1))
        law = [dict(shot_slope_dn=6.4, read_variance_dn2=156.)]*4
        variance = 6.4/DIVISOR*truth+156./DIVISOR**2
        count = 12
        frames = truth+rng.normal(size=(count,)+truth.shape)*np.sqrt(variance)
        others = frames[1:].mean(axis=0)
        valid = np.ones(truth.shape, bool)
        scored = same_burst_score(truth, frames[0], others, count, law, valid)
        self.assertAlmostEqual(scored['corrected_observed_mse']/float(variance[:, 32:-32, 32:-32].mean()), 1., delta=.03)
        self.assertLess(abs(scored['corrected_mse']), .03*scored['corrected_observed_mse'])


if __name__ == '__main__':
    unittest.main()
