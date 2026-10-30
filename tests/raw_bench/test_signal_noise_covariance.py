import unittest
import numpy as np
from signal_noise_covariance import transported_covariance
from study_cfa_signal_transport import camera_planes,covariance


class TransportCovarianceContracts(unittest.TestCase):
    def test_constant_signed_input_has_zero_band_covariance(self):
        result=transported_covariance(np.full((256,256,3),-.03))
        self.assertEqual(result.shape,(5,3,3))
        self.assertLess(float(np.max(np.abs(result))),1e-25)

    def test_first_three_bands_match_previous_transport(self):
        rgb=np.random.default_rng(531).normal(size=(256,256,3))*.001
        bands=transported_covariance(rgb)
        old=camera_planes(rgb)
        for level in range(3):np.testing.assert_array_equal(bands[level],covariance(old[f'B3_{level}']))
        for matrix in bands:self.assertGreaterEqual(np.linalg.eigvalsh(matrix).min(),0)

    def test_invalid_shape_and_nonfinite_fail(self):
        for bad in (np.zeros((256,256)),np.full((256,256,3),np.nan)):
            with self.assertRaises(ValueError):transported_covariance(bad)


if __name__=='__main__':unittest.main()
