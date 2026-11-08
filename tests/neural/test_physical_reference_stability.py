import unittest
import numpy as np
from physical_reference_stability import compare_bursts


class ReferenceStabilityTests(unittest.TestCase):
    def test_independent_noise_mean_difference_matches_predicted_variance(self):
        rng=np.random.default_rng(72)
        a=rng.normal(512,2,(9,2,4,64,64));b=rng.normal(512,2,(8,2,4,64,64))
        rows=compare_bursts(a,b)
        self.assertTrue(all(.9<r['mse_to_expected_ratio']<1.1 for r in rows))

    def test_illumination_shift_not_hidden_by_recentering(self):
        rng=np.random.default_rng(74)
        a=rng.normal(512,2,(9,1,4,64,64));b=rng.normal(522,2,(8,1,4,64,64))
        self.assertTrue(all(r['mean_difference_dn']>9 and r['mse_to_expected_ratio']>50 for r in compare_bursts(a,b)))


if __name__=='__main__':unittest.main()
