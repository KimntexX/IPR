import copy
import unittest
from run_cfa_prototype import summarize


class CfaPrototypeScreenTests(unittest.TestCase):
    @staticmethod
    def case():
        return dict(noisy={name:dict(rgb_mse_truth=value) for name,value in
                          (('rcd_only',1.),('cfa_wiener',.2),('post_current',.5),('cfa_plus_post',.1))},
                    clean_controls={name:dict(pattern_gain=value) for name,value in
                          (('rcd_only',1.),('cfa_wiener',.97),('post_current',1.),('cfa_plus_post',.96))})

    def test_requires_noise_and_detail_gates(self):
        self.assertTrue(summarize([self.case()])['pre_vs_post_screen']['proceed_as_is'])

    def test_low_mse_does_not_hide_detail_loss(self):
        case=self.case();case['clean_controls']['cfa_wiener']['pattern_gain']=.8
        self.assertFalse(summarize([case])['pre_vs_post_screen']['proceed_as_is'])

    def test_clean_detail_alone_does_not_hide_worse_noise(self):
        case=self.case();case['noisy']['cfa_wiener']['rgb_mse_truth']=.6
        self.assertFalse(summarize([case])['pre_vs_post_screen']['proceed_as_is'])

    def test_flat_controls_are_not_fake_unit_contrast(self):
        flat=copy.deepcopy(self.case())
        for metrics in flat['clean_controls'].values():
            metrics['pattern_gain']=None
        result=summarize([flat,self.case()])
        self.assertEqual(result['cfa_wiener']['clean_pattern_cases'],1)
        self.assertEqual(result['cfa_wiener']['noisy_cases'],2)


if __name__=='__main__':
    unittest.main()
