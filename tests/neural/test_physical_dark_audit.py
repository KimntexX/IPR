import unittest
import numpy as np
from physical_dark_audit import statistics


class PhysicalDarkTests(unittest.TestCase):
    def test_constant_codes_do_not_create_nan_correlation(self):
        d=statistics(np.full((2,1,4,32,32),512,dtype=np.uint16))
        self.assertTrue(all(v is None for row in d['interplane_correlation'] for v in row))
        self.assertTrue(all(r['temporal_variance_dn2']==0 for r in d['rows']))

    def test_white_noise_has_correct_variance_and_small_correlations(self):
        x=np.random.default_rng(15).normal(512,2,(16,2,4,32,32))
        d=statistics(x)
        self.assertTrue(all(abs(r['temporal_variance_dn2']-4)<.25 for r in d['rows']))
        self.assertTrue(all(abs(v)<.05 for r in d['rows'] for v in r['correlations'].values()))
        self.assertTrue(all(r['half_burst_relative_variance_difference'] is not None for r in d['rows']))

    def test_common_pattern_survives_as_shared_structure(self):
        x=np.random.default_rng(18).normal(512,1,(16,1,4,32,32))
        x+=10*np.sin(np.arange(32)[None,None,None,None,:]*.4)
        d=statistics(x)
        self.assertTrue(all(r['shared_spatial_variance_debiased_dn2']>30 for r in d['rows']))
        self.assertTrue(all(abs(r['temporal_variance_dn2']-1)<.1 for r in d['rows']))

    def test_two_frames_not_declared_stable_burst(self):
        x=np.random.default_rng(25).normal(0,1,(2,1,4,32,32))
        self.assertTrue(all(r['half_burst_relative_variance_difference'] is None for r in statistics(x)['rows']))
        with self.assertRaises(ValueError):statistics(x[:1])


if __name__=='__main__':unittest.main()
