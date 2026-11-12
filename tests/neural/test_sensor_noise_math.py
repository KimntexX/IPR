import unittest
import numpy as np
from sensor_noise_math import calibration_pairs,pair_statistics,fit_signal_variance,near_white_fraction


class SensorNoiseTests(unittest.TestCase):
    def test_saturation_below_advertised_white_is_visible_to_guard(self):
        tiles=np.full((1,4,32,32),16382,dtype=np.uint16)
        self.assertEqual(float((tiles>=16383).mean()),0)
        np.testing.assert_array_equal(near_white_fraction(tiles,16383),np.ones((1,4)))
        with self.assertRaises(ValueError):near_white_fraction(tiles,0)

    def test_fixed_pattern_and_mean_drift_are_not_temporal_variance(self):
        fixed=np.indices((32,32)).sum(axis=0)[None,None].astype(np.float64)*3+512
        result=pair_statistics(fixed,fixed+7)
        self.assertEqual(float(result['temporal_variance_dn2'][0,0]),0)
        self.assertEqual(float(result['mean_drift_dn'][0,0]),-7)
        self.assertGreater(float(result['spatial_variance_dn2'][0,0]),100)
        self.assertEqual(float(result['difference_mse_over2_dn2'][0,0]),24.5)

    def test_independent_known_noise_recovered_by_plane(self):
        rng=np.random.default_rng(381)
        scale=np.array([2,3,4,5])[None,:,None,None]
        a=512+rng.normal(size=(2,4,128,128))*scale
        b=512+rng.normal(size=a.shape)*scale
        result=pair_statistics(a,b)
        np.testing.assert_allclose(result['temporal_variance_dn2'],np.broadcast_to(scale.reshape(1,4)**2,(2,4)),rtol=.045)

    def test_signal_law_predicts_separate_levels_better_than_constant(self):
        training=[dict(signal_dn=x,variance_dn2=.7*x+9) for x in (10,100,1000)]
        held=[dict(signal_dn=x,variance_dn2=.7*x+9) for x in (30,300)]
        result=fit_signal_variance(training,held,9)
        self.assertAlmostEqual(result['shot_slope_dn'],.7)
        self.assertLess(result['median_relative_variance_error'],1e-10)
        self.assertGreater(result['median_constant_relative_variance_error'],.5)

    def test_missing_support_and_invalid_values_do_not_produce_coefficients(self):
        self.assertNotIn('shot_slope_dn',fit_signal_variance([],[],1))
        with self.assertRaises(ValueError):pair_statistics(np.full((1,4,32,32),np.nan),np.zeros((1,4,32,32)))
        with self.assertRaises(ValueError):fit_signal_variance([],[],-1)

    def test_pairs_respect_iso_day_disjointness_and_reserved_folders(self):
        def row(number,iso=3200,day='22',folder='iso-3200'):
            return dict(path=f'C:/tests/protocole/{folder}/6V1A{number:04d}.CR3',metadata=dict(model='R6III',
                iso=iso,exposure_s=.005,aperture=4.,focal_mm=50.,datetime_original=f'2026:10:{day} 12:00:00'))
        pairs,unused=calibration_pairs([row(1),row(2),row(3),row(4,iso=1600),row(5,day='23'),row(6,folder='reserved')])
        self.assertEqual(len(pairs),1)
        self.assertEqual([r['path'] for r in pairs[0]['frames']],[row(1)['path'],row(2)['path']])
        self.assertEqual(len(unused),3)


if __name__=='__main__':unittest.main()
