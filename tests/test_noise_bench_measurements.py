import json
import unittest
import numpy as np
from noise_bench.contracts import field, mask_for
from noise_bench.blacks import black_repeatability
from noise_bench.residuals import describe_residual
from noise_bench.regions import region_support, describe_region


class MeasurementTests(unittest.TestCase):
    def test_contracts(self):
        np.testing.assert_array_equal(field(np.ones((4, 1, 1)), 'cfa'), 1)
        for a, domain in [(np.ones((3, 1, 1)), 'cfa'), (np.ones((1, 2, 2)), 'rgb'),
                          (np.empty((4, 0, 2)), 'cfa'), (np.full((4, 2, 2), np.nan), 'cfa')]:
            with self.assertRaises(ValueError):
                field(a, domain)
        for m in (np.ones((2, 2)), np.ones((1, 2), bool), np.zeros((2, 2), bool)):
            with self.assertRaises(ValueError):
                mask_for((2, 2), m)

    def test_black_pedestal_and_centered_noise_are_separate(self):
        pattern = np.array([[-1., 1], [1, -1]])
        first = np.stack([np.full((4, 2, 2), 100.)+pattern,
                          np.full((4, 2, 2), 102.)-pattern])
        result = black_repeatability(first, first+3, ['a', 'b'], ['c', 'd'],
            iso=800, black_dn=[100]*4, capture_kind='closed_lens')
        np.testing.assert_allclose(result['sets'][0]['mean_bias_dn'], 1)
        np.testing.assert_allclose(result['sets'][0]['frame_mean_std_dn'], np.sqrt(2))
        np.testing.assert_allclose(result['sets'][0]['centered_temporal_variance_dn2'], 2)
        np.testing.assert_allclose(result['second_minus_first_pedestal_dn'], 3)
        for kwargs in [dict(capture_kind='dark_scene'), dict(second_ids=['b', 'c']),
                       dict(black_dn=[100]), dict(iso=0), dict(first=first[:1], first_ids=['a'])]:
            args = dict(first=first, second=first, first_ids=['a', 'b'], second_ids=['c', 'd'],
                        iso=800, black_dn=[100]*4, capture_kind='closed_lens')
            args.update(kwargs)
            with self.assertRaises(ValueError):
                black_repeatability(**args)

    def test_signed_distribution_covariance_and_lags(self):
        x = np.array([[[-2., -1, 0, 1, 2], [-2, -1, 0, 1, 2]]])
        a = np.concatenate([x, -2*x], axis=0)
        result = describe_residual(a, domain='rendered', unit='linear RGB', lags=[(0, 1), (0, -1)])
        np.testing.assert_allclose(result['mean'], 0)
        np.testing.assert_allclose(result['covariance'], [[20/9, -40/9], [-40/9, 80/9]])
        np.testing.assert_allclose(result['negative_fraction'], .4)
        self.assertAlmostEqual(result['moments'][0]['excess_kurtosis'], -1.3)
        for row in result['lag_correlations']:
            np.testing.assert_allclose(row['by_plane'], 1)
        self.assertEqual(result['quantiles'][1], [0, 0])

    def test_constant_and_masked_residuals_remain_json_serializable(self):
        a = np.ones((4, 3, 3))
        result = describe_residual(a, domain='cfa', unit='DN', lags=[(0, 1)])
        self.assertIsNone(result['moments'][0]['skewness'])
        self.assertEqual(result['lag_correlations'][0]['by_plane'], [None]*4)
        json.dumps(result, allow_nan=False)
        mask = np.zeros((3, 3), bool)
        mask[:, 0] = True
        result = describe_residual(a, domain='cfa', unit='DN', mask=mask, lags=[(0, 1)])
        self.assertEqual(result['lag_correlations'][0]['valid_pairs'], 0)
        for kwargs in [dict(lags=[(0, 0)]), dict(lags=[(3, 0)]), dict(unit=''),
                       dict(mask=np.eye(3, dtype=bool) & (np.arange(3)[:, None] == 0))]:
            args = dict(domain='cfa', unit='DN')
            args.update(kwargs)
            with self.assertRaises(ValueError):
                describe_residual(a, **args)

    def test_roi_support_context_holes_and_known_ramp(self):
        self.assertTrue(region_support((8, 8), (1, 1, 6, 6), halo=1))
        for rect, halo in [((0, 0, 8, 8), 1), ((1, 1, 0, 2), 0), ((1., 1, 2, 2), 0), ((1, 1, 2, 2), -1)]:
            self.assertFalse(region_support((8, 8), rect, halo=halo))
        yy, xx = np.indices((8, 8))
        a = (2*xx+3*yy)[None]
        result = describe_region(a, (2, 2, 3, 3), domain='rendered', halo=2)
        self.assertEqual(result['gradient_energy'], [13.])
        self.assertEqual(result['span'], [10.])
        mask = np.ones((8, 8), bool)
        mask[0, 0] = False
        self.assertFalse(describe_region(a, (2, 2, 3, 3), domain='rendered', halo=2, mask=mask)['eligible'])
        with self.assertRaises(ValueError):
            describe_region(a, (0, 0, 3, 3), domain='rendered', halo=2)

    def test_flat_and_single_pixel_regions(self):
        for rect in [(0, 0, 4, 4), (1, 1, 1, 1)]:
            result = describe_region(np.ones((4, 4, 4)), rect, domain='cfa', halo=0)
            self.assertTrue(result['eligible'])
            self.assertEqual(result['constant_by_plane'], [True]*4)
            self.assertEqual(result['gradient_energy'], [0]*4)
            self.assertFalse(result['texture_noise_separation_verified'])


if __name__ == '__main__':
    unittest.main()
