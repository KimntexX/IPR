import unittest
import numpy as np
from camera_noise_sigma import interpolate_noise,balanced_sigma


class CameraNoiseSigmaTests(unittest.TestCase):
    def test_dark_floor_gain_and_signal_conversion(self):
        signal=np.array([-100,0,100],dtype=np.float32)
        np.testing.assert_allclose(balanced_sigma(signal,2,9,2,100),[.06,.06,np.sqrt(209)*.02],rtol=1e-6)
        np.testing.assert_array_equal(signal,[-100,0,100])

    def test_interpolation_exact_nodes_and_no_extrapolation(self):
        fits=[dict(iso=x,plane=2,fit=dict(shot_slope_dn=x/100),reference_dark_variance_dn2=(x/100)**2) for x in (100,400)]
        a,b=interpolate_noise(fits,200,2)
        self.assertAlmostEqual(a,2);self.assertAlmostEqual(b,4)
        with self.assertRaises(ValueError):interpolate_noise(fits,50,2)
        with self.assertRaises(ValueError):interpolate_noise(fits,200,1)


if __name__=='__main__':unittest.main()
