import unittest
import numpy as np
from joint_physical_noise import correlations


class JointPhysicalNoiseTests(unittest.TestCase):
    def test_known_cross_plane_correlation_and_unbalanced_variances(self):
        values=np.random.default_rng(18).normal(size=(128,4,48,48))
        values[:,1]=3*(.3*values[:,0]+np.sqrt(1-.3**2)*values[:,1])
        result=correlations(values)
        self.assertEqual(result.shape,(128,6))
        self.assertAlmostEqual(float(result[:,0].mean()),.3,delta=.01)
        self.assertLess(float(np.abs(result[:,1:].mean(axis=0)).max()),.01)

    def test_dc_offsets_do_not_become_correlation(self):
        values=np.random.default_rng(19).normal(size=(4,64,64))
        shifted=values+np.arange(4)[:,None,None]*100
        np.testing.assert_allclose(correlations(values),correlations(shifted),atol=1e-13)

    def test_missing_plane_and_zero_power_refused(self):
        for values in (np.ones((3,32,32)),np.ones((4,32,32)),np.full((4,32,32),np.nan)):
            with self.assertRaises(ValueError):
                correlations(values)
