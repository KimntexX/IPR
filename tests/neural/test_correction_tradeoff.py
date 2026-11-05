import unittest

import torch

from study_correction_tradeoff import blend,cfa_measure
from summarize_correction_tradeoff import summarize_group


class CorrectionTradeoffTests(unittest.TestCase):
    def test_endpoints_exact_and_intermediate_does_not_mutate_source(self):
        values=torch.tensor([[[[.2,.3],[.4,.5]]]]).expand(1,4,2,2).clone()
        corrected=values+.1
        before=values.clone()
        self.assertTrue(torch.equal(blend(values,corrected,0.),values))
        self.assertTrue(torch.equal(blend(values,corrected,1.),corrected))
        self.assertTrue(torch.allclose(blend(values,corrected,.5),(values+corrected)/2))
        self.assertTrue(torch.equal(values,before))
        for invalid in (True,-.1,1.1,float('nan')):
            with self.assertRaises(ValueError):
                blend(values,corrected,invalid)

    def test_flat_bias_is_not_reported_as_spatial_variance(self):
        clean=torch.full((1,4,24,24),.2)
        observed=clean+.01
        measured=cfa_measure(observed,clean,observed,8)
        self.assertAlmostEqual(measured['mse_over_noisy'],1.)
        self.assertEqual(measured['spatial_variance'],0.)
        self.assertGreater(measured['mse'],0.)

    def test_summary_keeps_noise_and_transfer_separate_and_refuses_zero_parent(self):
        measured=dict(scale=1.,rgb=dict(sine_gain=[.8,.9,1.],rgb_mse=.03),cfa=dict(mse_over_noisy=.5,spatial_variance=.01))
        rows=[dict(case=dict(amplitude=.06,frequency=10,chromatic=False),field=dict(case_index=0),variants=[measured])]
        parents={0:dict(rgb=dict(sine_gain=[1.,1.,1.]))}
        result=summarize_group(rows,[dict(variants=[measured])],parents,1.)
        self.assertEqual(result['minimum_strong_transfer_over_parent'],.8)
        self.assertEqual(result['mean_constant_cfa_mse_over_noisy'],.5)
        parents[0]['rgb']['sine_gain'][0]=0.
        with self.assertRaises(ValueError):
            summarize_group(rows,[dict(variants=[measured])],parents,1.)
