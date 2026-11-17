import unittest

import numpy as np

from camera_finetune import bursts, plateau_reached, row_matched

PLAN = dict(min_steps=15000, plateau=dict(window_controls=4, gain_db=.02, metrics=['d60_synthetic', 'd60_real']))


def controls(synthetic, real, every=2500):
    return [dict(step=i*every, summary=dict(d60_synthetic=s, d60_real=r)) for i, (s, r) in enumerate(zip(synthetic, real))]


class PlateauTests(unittest.TestCase):
    def test_no_stop_before_the_minimum_steps_even_when_flat(self):
        self.assertFalse(plateau_reached(controls([1.]*6, [1.]*6), PLAN))  # last control at 12 500

    def test_stops_when_both_metrics_are_flat_over_the_window(self):
        flat = [0., .5, .8, .9, .905, .91, .91, .91]
        self.assertTrue(plateau_reached(controls(flat, flat), PLAN))

    def test_one_metric_still_improving_keeps_training(self):
        flat = [0., .5, .8, .9, .905, .91, .91, .91]
        rising = [0., .1, .2, .3, .4, .5, .6, .7]
        self.assertFalse(plateau_reached(controls(flat, rising), PLAN))
        self.assertFalse(plateau_reached(controls(rising, flat), PLAN))

    def test_a_late_regression_counts_as_a_plateau(self):
        falling = [0., .5, .9, 1., .9, .8, .7, .6]
        self.assertTrue(plateau_reached(controls(falling, falling), PLAN))

    def test_too_few_controls_never_stop(self):
        self.assertFalse(plateau_reached(controls([0.]*4, [0.]*4, every=10000), PLAN))


class RowMatchTests(unittest.TestCase):
    def test_removes_a_row_gain_and_keeps_a_matched_reference(self):
        rng = np.random.default_rng(1)
        scene = .2+.1*rng.random((4, 64, 64))
        rows = 1+.05*np.sin(np.arange(64)/6)[None, :, None]
        frame = scene*rows
        matched = row_matched(scene, frame)
        self.assertLess(np.abs(matched/frame-1)[:, 8:-8].max(), .01)
        np.testing.assert_allclose(row_matched(frame, frame)[:, 8:-8], frame[:, 8:-8], rtol=1e-12)

    def test_dark_rows_are_left_unscaled(self):
        reference = np.zeros((4, 32, 32))
        np.testing.assert_array_equal(row_matched(reference, reference+.5), reference)


class BurstTests(unittest.TestCase):
    def test_consecutive_frames_of_one_setting_form_a_burst_of_three_or_more(self):
        frames = [dict(iso=100, exposure_s=.1, path=str(i)) for i in range(3)]
        frames += [dict(iso=100, exposure_s=.05, path='x'), dict(iso=100, exposure_s=.05, path='y')]
        frames += [dict(iso=400, exposure_s=.1, path=str(i)) for i in range(4)]
        groups = bursts(frames)
        self.assertEqual([(g['key'], len(g['frames'])) for g in groups], [((100, .1), 3), ((400, .1), 4)])


if __name__ == '__main__':
    unittest.main()
