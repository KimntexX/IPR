import unittest
import numpy as np
from physical_replay_probe import differences,metrics


class PhysicalReplayTests(unittest.TestCase):
    def test_poisson_gain_has_expected_variance(self):
        dark=np.round(np.random.default_rng(45).normal(512,2,(32,64,64))).astype(np.uint16)
        result=metrics(differences(dark,1000,2,47,repetitions=16))
        self.assertAlmostEqual(result['pair_variance_dn2']/2004,1,delta=.05)

    def test_static_pattern_cancels_and_zero_signal_adds_no_photons(self):
        dark=np.full((4,32,32),512,dtype=np.uint16);dark[:,:,5]=600
        np.testing.assert_array_equal(differences(dark,0,2,49),0)

    def test_invalid_library_or_negative_signal_refused(self):
        with self.assertRaises(ValueError):differences(np.zeros((1,32,32)),10,2,50)
        with self.assertRaises(ValueError):differences(np.zeros((4,32,32)),-1,2,50)


if __name__=='__main__':unittest.main()
