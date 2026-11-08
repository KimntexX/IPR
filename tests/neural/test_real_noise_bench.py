import unittest

import numpy as np

from real_noise_bench import files, gain_offset, phase_shift, split_half_ratio


class RealNoiseBenchTests(unittest.TestCase):
    def test_phase_correlation_finds_an_integer_translation(self):
        rng = np.random.default_rng(3)
        image = rng.normal(size=(128, 160)).cumsum(axis=0).cumsum(axis=1)
        moved = np.roll(image, (5, -7), axis=(0, 1))
        shift, sharpness = phase_shift(moved, image)
        self.assertEqual(shift, [5, -7])
        self.assertGreater(sharpness, 10)

    def test_split_half_ratio_is_one_for_a_static_scene_and_grows_with_motion(self):
        rng = np.random.default_rng(1)
        scene = rng.uniform(size=(4, 64, 64))
        frames = scene+rng.normal(0, .01, size=(16, 4, 64, 64))
        temporal = np.full((4, 1, 1), 1e-4)
        static = split_half_ratio(frames[:8].sum(0), 8, frames[8:].sum(0), 8, temporal)
        self.assertAlmostEqual(static, 1., delta=.15)
        per_plane = split_half_ratio(frames[:8].sum(0), 8, frames[8:].sum(0), 8, np.full(4, 1e-4))
        self.assertAlmostEqual(per_plane, static, places=12)
        frames[8:] += np.roll(scene, 1, axis=-1)-scene
        self.assertGreater(split_half_ratio(frames[:8].sum(0), 8, frames[8:].sum(0), 8, temporal), 10)

    def test_gain_offset_recovers_exposure_scaling(self):
        reference = np.random.default_rng(2).uniform(size=(4, 32, 32))
        fitted = gain_offset(reference, reference/1.6-.01)
        for row in fitted:
            self.assertAlmostEqual(row['gain'], 1.6, places=6)
            self.assertAlmostEqual(row['offset'], .016, places=6)

    def test_file_ranges_are_inclusive(self):
        self.assertEqual([p.name for p in files(9, 11)], ['6V1A0009.CR3', '6V1A0010.CR3', '6V1A0011.CR3'])


if __name__ == '__main__':
    unittest.main()
