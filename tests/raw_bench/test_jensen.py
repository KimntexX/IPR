import unittest
from audit_jensen import decomposition


class JensenDecompositionTests(unittest.TestCase):
    @staticmethod
    def states(before,after,f0,f1):
        return dict(B0=dict(mean_of_F=dict(y=before),F_of_mean=dict(y=f0)),
                    B100=dict(mean_of_F=dict(y=after),F_of_mean=dict(y=f1)))

    def test_mean_shift_only(self):
        result=decomposition(self.states(1.,3.,1.,3.),'y')
        self.assertEqual(result['mean_shift_component'],2.)
        self.assertEqual(result['distribution_gap_component'],0.)

    def test_concave_noise_gap_can_raise_rendered_mean(self):
        result=decomposition(self.states(8.,10.,10.,10.),'y')
        self.assertEqual(result['observed_delta'],2.)
        self.assertEqual(result['gap_before'],-2.)
        self.assertEqual(result['mean_shift_component'],0.)

    def test_positive_noise_gap_can_lower_rendered_mean(self):
        result=decomposition(self.states(12.,10.,10.,10.),'y')
        self.assertEqual(result['observed_delta'],-2.)
        self.assertEqual(result['gap_before'],2.)

    def test_opposing_components_are_not_percentages(self):
        result=decomposition(self.states(10.,13.,2.,12.),'y')
        self.assertEqual(result['mean_shift_component'],10.)
        self.assertEqual(result['distribution_gap_component'],-7.)
        self.assertEqual(result['observed_delta'],3.)

    def test_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            decomposition(self.states(float('nan'),1.,0.,0.),'y')


if __name__=='__main__':
    unittest.main()
