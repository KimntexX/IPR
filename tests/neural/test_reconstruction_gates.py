import unittest
from reconstruction_gates import gate_config,qualifies


class ReconstructionGateTests(unittest.TestCase):
    def fixture(self):
        return dict(summary=dict(mse_ratio=.8,opponent_mse_ratio=.8),records=[dict(observed_site_mse=0.)],
            by_family={n:dict(mse_ratio=.8,opponent_mse_ratio=.8,baseline_mse=1e-7,output_mse=1e-8,
                baseline_gradient_gain=1.,output_gradient_gain=1.) for n in
                ('constant','ramp','edge','weak_achromatic_wave','chromatic_wave')})

    def test_all_requirements_needed_before_noise_stage(self):
        result=self.fixture();self.assertTrue(qualifies(result,gate_config())['passed'])
        result['by_family']['edge']['mse_ratio']=1.1
        qualified=qualifies(result,gate_config())
        self.assertFalse(qualified['passed']);self.assertEqual(qualified['decision'],'stop_before_noise_stage')

    def test_identity_baseline_is_not_an_improvement(self):
        result=self.fixture();result['summary']['mse_ratio']=1.
        self.assertFalse(qualifies(result,gate_config())['passed'])

    def test_structure_and_observed_site_failures_not_hidden_by_mse(self):
        for field in ('structure','observed'):
            with self.subTest(field=field):
                result=self.fixture()
                if field=='structure':result['by_family']['weak_achromatic_wave']['output_gradient_gain']=.9
                else:result['records'][0]['observed_site_mse']=2e-6
                self.assertFalse(qualifies(result,gate_config())['passed'])

    def test_undefined_and_nonfinite_scores_do_not_pass(self):
        for value in (None,float('nan'),float('inf')):
            result=self.fixture();result['summary']['mse_ratio']=value
            self.assertFalse(qualifies(result,gate_config())['passed'])
        result=self.fixture();result['records']=[]
        with self.assertRaises(ValueError):qualifies(result,gate_config())


if __name__=='__main__':unittest.main()
