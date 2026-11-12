import unittest
import numpy as np
from fit_photon_covariance import fit_coefficients,colour_innovations,covariances


class PhotonCovarianceTests(unittest.TestCase):
    def test_fit_recovers_only_shot_part_after_read_covariance(self):
        rows=[dict(shot_variance=s,observed_x=.2+.03*s,observed_y=.1-.01*s,read_x=.2,read_y=.1) for s in (10,100,1000)]
        fit=fit_coefficients(rows)
        self.assertAlmostEqual(fit['gamma_x'],.03);self.assertAlmostEqual(fit['gamma_y'],-.01)
        self.assertTrue(fit['positive_spectrum'])

    def test_colouring_preserves_variance_and_creates_expected_covariance(self):
        x=np.random.default_rng(91).normal(size=(64,64,64))
        y=colour_innovations(x,.04,.02);m=covariances(y)
        self.assertAlmostEqual(float(m['variance'].mean()),1,delta=.02)
        self.assertAlmostEqual(float(m['x'].mean()),.04,delta=.01)
        self.assertAlmostEqual(float(m['y'].mean()),.02,delta=.01)
        np.testing.assert_allclose(colour_innovations(x,0,0),x,atol=1e-12)

    def test_nonpositive_spectrum_refused(self):
        with self.assertRaises(ValueError):colour_innovations(np.ones((32,32)),.4,.2)
        with self.assertRaises(ValueError):colour_innovations(np.ones((32,32)),np.nan,0)
        with self.assertRaises(ValueError):fit_coefficients([dict(shot_variance=1,observed_x=np.nan,
            observed_y=0,read_x=0,read_y=0)])


if __name__=='__main__':unittest.main()
