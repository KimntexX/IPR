import unittest

import numpy as np
import torch

from physical_natural_training import (ADC_MAX, TARGET_BLACK, color_augment, covariance_filter, grid_origins, noise_parameters,
                                       pack_rggb, plane_pedestals, sigma_map, synthesise)


class GeometryTests(unittest.TestCase):
    def test_grid_tiles_are_even_disjoint_and_inside_margins(self):
        origins = grid_origins(1200, 1700, tile=512, margin=32)
        self.assertEqual(origins, [(32, 32), (32, 544), (32, 1056), (544, 32), (544, 544), (544, 1056)])
        self.assertTrue(all(y % 2 == 0 and x % 2 == 0 for y, x in origins))

    def test_grid_rejects_small_or_odd_geometry(self):
        with self.assertRaises(ValueError):
            grid_origins(500, 2000)
        with self.assertRaises(ValueError):
            grid_origins(2000, 2000, tile=511)

    def test_packing_keeps_raster_bayer_positions(self):
        mosaic = np.array([[0, 1, 0, 1], [3, 2, 3, 2]], dtype=np.uint16)
        packed = pack_rggb(mosaic)
        self.assertEqual(packed.shape, (4, 1, 2))
        self.assertEqual(packed[:, 0, 0].tolist(), [0, 1, 3, 2])
        with self.assertRaises(ValueError):
            pack_rggb(np.zeros((3, 4)))

    def test_pedestals_are_per_position_means(self):
        mosaic = np.tile(np.array([[10, 20], [30, 40]], dtype=np.uint16), (4, 4))
        self.assertEqual(plane_pedestals(mosaic).tolist(), [10., 20., 30., 40.])


class NoiseTests(unittest.TestCase):
    def test_parameters_require_a_measured_positive_slope(self):
        bank = dict(fits=[dict(iso=12800, plane=p, fit=dict(shot_slope_dn=None), reference_dark_variance_dn2=1.)
                          for p in range(4)])
        covariance = dict(coefficients=[dict(iso=12800, plane=p, gamma_x=0., gamma_y=0., positive_spectrum=True)
                                        for p in range(4)])
        with self.assertRaises(ValueError):
            noise_parameters(bank, covariance, isos=(12800,))

    def test_zero_covariance_filter_is_identity(self):
        filters = covariance_filter([0.]*4, [0.]*4, 16)
        torch.testing.assert_close(filters, torch.ones(4, 16, 16))
        with self.assertRaises(ValueError):
            covariance_filter([.3]*4, [.3]*4, 16)

    def test_flat_field_variance_matches_shot_plus_dark(self):
        g = torch.Generator().manual_seed(5)
        divisor, slope, level = 12303., 6.5, .2
        clean = torch.full((2, 4, 128, 128), level)
        dark = torch.randn((2, 4, 128, 128), generator=g)*12.
        noisy = synthesise(clean, torch.full((2, 4, 1, 1), slope), dark, torch.ones(2, 4, 128, 128), divisor, g)
        variance = float(((noisy-clean)*divisor).var())
        expected = slope*level*divisor+144.+1/12
        self.assertAlmostEqual(variance/expected, 1., delta=.03)
        self.assertAlmostEqual(float((noisy-clean).mean()*divisor), 0., delta=1.)

    def test_negative_proxies_are_kept_and_adc_clips_in_sensor_units(self):
        g = torch.Generator().manual_seed(1)
        divisor = 12303.
        clean = torch.tensor([-.01, 0., 1.3]).reshape(1, 1, 1, 3).expand(1, 4, 8, 3).contiguous()
        noisy = synthesise(clean, torch.full((1, 4, 1, 1), 1e-6), torch.zeros_like(clean),
                           torch.ones(1, 4, 8, 3), divisor, g)
        self.assertTrue((noisy[..., 0] < 0).all())
        self.assertTrue(torch.allclose(noisy[..., 2], torch.full_like(noisy[..., 2], (ADC_MAX-TARGET_BLACK)/divisor)))

    def test_sigma_map_is_bounded_below_by_read_noise_and_uses_noisy_input_only(self):
        noisy = torch.full((1, 4, 16, 16), -.1)
        sigma = sigma_map(noisy, torch.full((1, 4, 1, 1), 6.), torch.full((1, 4, 1, 1), 100.), 1000.)
        torch.testing.assert_close(sigma, torch.full_like(sigma, .01))
        bright = sigma_map(torch.full((1, 4, 16, 16), .5), torch.full((1, 4, 1, 1), 6.),
                           torch.full((1, 4, 1, 1), 100.), 1000.)
        torch.testing.assert_close(bright, torch.full_like(bright, (6e-3*.5+1e-4)**.5))


class ColorAugmentationTests(unittest.TestCase):
    def test_gains_touch_red_and_blue_only_within_range_and_never_cross_white(self):
        clean = torch.rand(64, 4, 8, 8, dtype=torch.float64)*.9+.05
        clean[0, :, 0, 0] = 1.2
        out = color_augment(clean, torch.Generator().manual_seed(4), 1., (.33, 2.5))
        torch.testing.assert_close(out[:, 1:3], clean[:, 1:3])
        ratio = (out[:, [0, 3]]/clean[:, [0, 3]])
        unclipped = out[:, [0, 3]] < torch.maximum(clean[:, [0, 3]], torch.ones_like(clean[:, [0, 3]]))
        self.assertTrue(((ratio[unclipped] >= .33-1e-9) & (ratio[unclipped] <= 2.5+1e-9)).all())
        self.assertTrue((out <= torch.maximum(clean, torch.ones_like(clean))+1e-12).all())
        self.assertEqual(float(out[0, 0, 0, 0]), 1.2 if float(ratio[0, 0, 0, 0]) >= 1 else float(out[0, 0, 0, 0]))
        per_sample = ratio[:, :, 1, 1]
        self.assertGreater(float(per_sample.std()), .2)

    def test_probability_zero_and_invalid_ranges(self):
        clean = torch.rand(8, 4, 4, 4)
        torch.testing.assert_close(color_augment(clean, torch.Generator().manual_seed(1), 0., (.5, 2.)), clean)
        with self.assertRaises(ValueError):
            color_augment(clean, torch.Generator().manual_seed(1), .5, (.5, 2.), (1.01, 1.02))
        for probability, gains in ((1.5, (.5, 2.)), (.5, (1.2, 2.)), (.5, (.5, .9))):
            with self.assertRaises(ValueError):
                color_augment(clean, torch.Generator().manual_seed(1), probability, gains)

    def test_green_gains_are_small_independent_and_follow_their_range(self):
        clean = torch.full((256, 4, 4, 4), .3, dtype=torch.float64)
        out = color_augment(clean, torch.Generator().manual_seed(6), 1., (1., 1.), (.98, 1.02))
        g1, g2 = out[:, 1, 0, 0]/.3, out[:, 2, 0, 0]/.3
        self.assertTrue(((g1 >= .98-1e-12) & (g1 <= 1.02+1e-12)).all() and ((g2 >= .98-1e-12) & (g2 <= 1.02+1e-12)).all())
        self.assertGreater(float((g1-g2).abs().mean()), .005)
        torch.testing.assert_close(out[:, [0, 3]], clean[:, [0, 3]])


if __name__ == '__main__':
    unittest.main()
