import unittest
import numpy as np
from study_structure_prediction import TRAIN, CONTROL, samples
from study_structure_amplitude_free import fit_observation, shape, estimate, template, planes


class AmplitudeFreeContractTests(unittest.TestCase):
    def test_shape_does_not_use_reference_amplitude(self):
        rng = np.random.default_rng(15)
        reference = rng.normal(size=(256,256))
        first, second = shape(reference), shape(reference*37.)
        np.testing.assert_allclose(first[0],second[0],atol=1e-12)
        np.testing.assert_allclose(first[1],second[1],atol=1e-12)

    def test_fit_only_sees_observation_training(self):
        rng = np.random.default_rng(18)
        field = rng.normal(size=(256,256))
        weights = fit_observation(samples(field,TRAIN))[0]
        field[144:,144:] += 200
        np.testing.assert_array_equal(weights,fit_observation(samples(field,TRAIN))[0])

    def test_nonfinite_fit_rejected(self):
        x = np.zeros((100,16))
        x[0,0] = np.nan
        with self.assertRaises(ValueError):
            fit_observation((x,np.zeros(100)))

    def test_novel_details_not_in_training(self):
        for pattern in ('novel_edge','novel_line','novel_spot'):
            truth = template(pattern,.003)
            self.assertLess(float(np.var(samples(truth,TRAIN)[1])),1e-20)
            self.assertGreater(float(np.var(samples(truth,CONTROL)[1])),1e-12)

    def test_constant_pyramid_has_zero_details(self):
        for _,field in planes(np.full((256,256),.03))[1:]:
            self.assertLess(float(np.max(np.abs(field))),1e-15)

    def test_invalid_shape_and_denominator_are_explicit(self):
        with self.assertRaises(ValueError):
            shape(np.zeros((256,256)))
        w = np.zeros(16); w[0]=1
        variance, denominator = estimate(1,w,(np.eye(16),np.full(16,2.)))
        self.assertIsNone(variance)
        self.assertLess(denominator,0)


if __name__ == '__main__':
    unittest.main()
