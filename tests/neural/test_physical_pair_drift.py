import unittest
import numpy as np
from physical_pair_drift import project,smooth


class PairDriftTests(unittest.TestCase):
    def example(self):
        y,x=np.indices((48,48))
        source=1000+100*np.sin(x*.2)+80*np.cos(y*.17)
        return np.broadcast_to(source,(3,4,48,48)).copy()

    def test_signal_drift_recovered_without_destroying_injected_noise(self):
        mean=self.example();smoothed=smooth(mean);gy,gx=np.gradient(smoothed,axis=(-2,-1))
        noise=np.random.default_rng(64).normal(0,2,mean.shape)
        difference=.01*(smoothed-smoothed.mean())+.2*gx-.1*gy+3+noise
        residual,info=project(mean,difference,np.ones((3,4),bool))
        self.assertAlmostEqual(float(residual.var()),4,delta=.15)
        self.assertAlmostEqual(info['coefficients']['translation_x_packed'],.2,delta=.02)
        self.assertAlmostEqual(info['coefficients']['translation_y_packed'],-.1,delta=.02)

    def test_projection_does_not_hide_wrong_noise_variance(self):
        mean=self.example();rng=np.random.default_rng(66)
        residual,info=project(mean,rng.normal(0,4,mean.shape),np.ones((3,4),bool))
        self.assertAlmostEqual(float(residual.var()),16,delta=.6)
        self.assertLess(info['dof_fraction'],.001)

    def test_missing_mask_or_nonfinite_input_refused(self):
        mean=self.example()
        with self.assertRaises(ValueError):project(mean,np.zeros_like(mean),np.zeros((3,4),bool))
        with self.assertRaises(ValueError):project(mean,np.full_like(mean,np.nan),np.ones((3,4),bool))


if __name__=='__main__':unittest.main()
