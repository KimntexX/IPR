import unittest
import numpy as np
from noise_bench.units import convert_noise_law, coverage
from noise_bench.reference import crop_transform, paired_risk, audit_reference


class ReferenceTests(unittest.TestCase):
    def test_noise_law_preserves_variance_and_signed_intercept(self):
        law = convert_noise_law(2, -3, black_delta_dn=4, divisor_dn=10,
                                black_origin='independent dark captures')
        self.assertEqual(law['shifted_intercept_dn2'], 5)
        self.assertAlmostEqual(law['slope_normalized']*.6+law['intercept_normalized'], .17)
        for args in [(2, 3, 0, 0, 'dark'), (2, 3, 0, 1, ''), (float('nan'), 3, 0, 1, 'dark')]:
            a, b, d, s, origin = args
            with self.assertRaises(ValueError):
                convert_noise_law(a, b, black_delta_dn=d, divisor_dn=s, black_origin=origin)

    def test_coverage_edges_empty_bins_and_distinct_captures(self):
        rows = [dict(iso=100, signal_dn=s, capture_id='a') for s in (-1, 0, 1, 2)]
        result = coverage(rows, iso_values=[100, 200], signal_edges_dn=[0, 1, 2])
        self.assertEqual(result['outside_measurements'], 1)
        self.assertEqual([b['measurements'] for b in result['bins']], [1, 2, 0, 0])
        self.assertEqual(result['bins'][1]['distinct_captures'], 1)
        for edges in ([1, 1], [0, np.nan], [0]):
            with self.assertRaises(ValueError):
                coverage([], iso_values=[100], signal_edges_dn=edges)

    def test_affine_local_coordinates(self):
        source = np.array([[2., 0, 3], [0, 2, 4]])
        target = np.array([[1., 0, 5], [0, 1, 7]])
        transform = crop_transform(source, target, [10, 20])
        np.testing.assert_allclose(transform @ [2, 3, 1], [7, 16])
        with self.assertRaises(ValueError):
            crop_transform(source, np.zeros((2, 3)), [0, 0])

    def test_risk_known_zero_mean_independent_references(self):
        first = np.array([[[1., 1, -1, -1]]])
        second = np.array([[[1., -1, 1, -1]]])
        np.testing.assert_allclose(paired_risk(np.full_like(first, 2), first, second,
                                             domain='rendered'), [4])
        np.testing.assert_allclose(paired_risk(np.zeros_like(first), first, -first,
                                             domain='rendered'), [-1])
        with self.assertRaises(ValueError):
            paired_risk(first, first, second, domain='cfa')
        with self.assertRaises(ValueError):
            paired_risk(first, first, second, domain='rendered', mask=np.zeros((1, 4), bool))

    def test_reference_drift_and_common_pattern_limit(self):
        a = np.arange(4.)[:, None, None, None] + np.zeros((4, 4, 2, 2))
        result = audit_reference(a, list('abcd'), domain='cfa')
        np.testing.assert_allclose(result['temporal_variance_by_plane'], 5/3)
        np.testing.assert_allclose(result['mean_variance_if_independent_by_plane'], 5/12)
        np.testing.assert_allclose(result['half_difference_mse_by_plane'], 1)
        self.assertFalse(result['independence_verified'])
        common = np.arange(16.).reshape(4, 2, 2)
        stable = audit_reference(np.stack([common]*4), list('abcd'), domain='cfa')
        np.testing.assert_allclose(stable['temporal_variance_by_plane'], 0)
        with self.assertRaises(ValueError):
            audit_reference(a, list('abca'), domain='cfa')
        with self.assertRaises(ValueError):
            audit_reference(a[:3], list('abc'), domain='cfa')


if __name__ == '__main__':
    unittest.main()
