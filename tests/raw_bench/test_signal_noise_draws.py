import unittest
import numpy as np
from signal_noise_draws import normalized_draws
from study_cfa_signal_transport import simulate


class SignalDistributionContracts(unittest.TestCase):
    def setUp(self):
        self.slopes={p:100. for p in ('R','G1','G2','B')}
        self.gains={p:1. for p in self.slopes}

    def test_gaussian_exact_previous_stream(self):
        target=np.full((256,256),.03)
        actual=normalized_draws(np.random.default_rng(513),target,self.slopes,self.gains,10000.,'gaussian')
        np.testing.assert_array_equal(actual,np.random.default_rng(513).normal(size=target.shape))

    def test_scaled_poisson_has_same_first_two_moments_but_skew(self):
        target=np.full((256,256),.03)
        draws=normalized_draws(np.random.default_rng(513),target,self.slopes,self.gains,10000.,'scaled_poisson')
        self.assertLess(abs(float(draws.mean())),.015)
        self.assertLess(abs(float(draws.var())-1),.025)
        self.assertGreater(float(np.mean(draws**3)),.45)
        _,_,shot,budget=simulate(target,np.zeros_like(target),self.slopes,self.gains,10000.,draws)
        self.assertLess(abs(float(shot.var()/budget.mean())-1),.025)

    def test_zero_signal_finite_zero(self):
        target=np.zeros((256,256))
        draws=normalized_draws(np.random.default_rng(1),target,self.slopes,self.gains,10000.,'scaled_poisson')
        np.testing.assert_array_equal(draws,target)

    def test_reject_invalid_distribution_or_latent(self):
        for dist in ('unknown','scaled_poisson'):
            with self.assertRaises(ValueError):
                normalized_draws(np.random.default_rng(1),np.full((256,256),-.1),self.slopes,self.gains,10000.,dist)
        with self.assertRaises(ValueError):
            normalized_draws(np.random.default_rng(1),np.zeros((256,256)),self.slopes,self.gains,10000.,'unknown')


if __name__=='__main__':unittest.main()
