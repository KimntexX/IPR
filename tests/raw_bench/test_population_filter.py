import copy
import unittest
import numpy as np
from study_population_filter import fixture, gates, DOMAINS, PATTERNS, CALIBRATION, CONTROLS, GUIDE


class PopulationFilterTests(unittest.TestCase):
    def test_fixture_grid_and_seed_separation(self):
        for iso in (1600,51200):
            for pattern in PATTERNS:
                image=fixture(iso,pattern)
                self.assertEqual(image.shape,(256,256,3))
                self.assertTrue(np.isfinite(image).all())
                self.assertGreater(image.min(),0)
        self.assertFalse(set(CALIBRATION)&set(CONTROLS))
        self.assertNotIn(GUIDE,CALIBRATION+CONTROLS)
        with self.assertRaises(ValueError):fixture(1600,'unknown')

    def test_domains_and_color_or_detail_failures_stay_visible(self):
        baseline=dict(rgb_mse=1.,opponent_mse=[1.,1.,1.],opponent_bias=[0.,0.,0.],pattern_gain=1.)
        candidate=dict(rgb_mse=.8,opponent_mse=[.8,.8,.8],opponent_bias=[0.,0.,0.],pattern_gain=1.)
        rows=[dict(domain=d,pattern='weak_color',input_rgb_mse=1.,measurements={name:dict(noisy_100=copy.deepcopy(baseline),
            clean_100=copy.deepcopy(baseline),external_covariance_noisy=copy.deepcopy(candidate),
            external_covariance_clean_stress=copy.deepcopy(candidate)) for name in ('bank','spectral')}) for d in DOMAINS]
        self.assertTrue(gates(rows)['spectral_internal']['spectral']['passed'])
        rows[1]['measurements']['spectral']['external_covariance_clean_stress']['pattern_gain']=.95
        rows[1]['measurements']['bank']['external_covariance_noisy']['opponent_mse']=[.8,3.,3.]
        result=gates(rows)
        self.assertTrue(result['spectral_internal']['spectral']['passed'])
        self.assertFalse(result['real_read_replay']['spectral']['passed'])
        self.assertFalse(result['real_read_replay']['bank']['passed'])
        with self.assertRaises(ValueError):gates(rows[:1])


if __name__=='__main__':unittest.main()
