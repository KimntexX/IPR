import unittest
import numpy as np
from study_cfa_signal_transport import simulate,camera_planes,compare_covariance,CALIBRATION_SEEDS,CONTROL_SEEDS


class CfaSignalTransportTests(unittest.TestCase):
    def setUp(self):
        self.slopes={phase:3. for phase in ('R','G1','G2','B')}
        self.gains={phase:1. for phase in self.slopes}

    def test_read_residual_added_once_without_synthetic_dark_term(self):
        target=np.full((256,256),.003)
        read=np.full_like(target,-2.)
        observed,balanced_read,shot,budget=simulate(target,read,self.slopes,self.gains,1000.,np.zeros_like(target))
        np.testing.assert_array_equal(observed,np.full_like(observed,.001))
        np.testing.assert_allclose(balanced_read,-.002)
        self.assertEqual(float(np.max(np.abs(shot))),0)
        self.assertGreater(float(budget.min()),0)

    def test_dark_has_no_added_signal_variance_and_keeps_negative_values(self):
        target=np.zeros((256,256));read=np.full_like(target,-2.)
        observed,_,shot,budget=simulate(target,read,self.slopes,self.gains,1000.,np.ones_like(target))
        self.assertTrue(np.all(observed<0))
        self.assertEqual(float(np.max(np.abs(shot))),0)
        self.assertEqual(float(budget.max()),0)

    def test_white_balance_gain_scales_signal_noise_variance(self):
        target=np.full((256,256),.01);zeros=np.zeros_like(target)
        first=simulate(target,zeros,self.slopes,self.gains,1000.,zeros)[3]
        doubled={phase:2. for phase in self.slopes}
        second=simulate(target,zeros,self.slopes,doubled,1000.,zeros)[3]
        np.testing.assert_allclose(second,2*first)

    def test_negative_covariance_extrapolation_not_projected(self):
        result=compare_covariance(np.diag([-1.,1,1]),np.eye(3))
        self.assertFalse(result['positive_semidefinite'])
        self.assertIsNone(result['sigma_predicted_over_observed'][0])

    def test_splits_disjoint_and_constant_wavelet_details_zero(self):
        self.assertFalse(set(CALIBRATION_SEEDS)&set(CONTROL_SEEDS))
        for name,field in camera_planes(np.full((256,256,3),.03)).items():
            if name!='pixel':self.assertLess(float(np.max(np.abs(field))),1e-15)

    def test_invalid_latent_rejected(self):
        bad=np.full((256,256),-.01);zeros=np.zeros_like(bad)
        with self.assertRaises(ValueError):
            simulate(bad,zeros,self.slopes,self.gains,1000.,zeros)


if __name__=='__main__':
    unittest.main()
