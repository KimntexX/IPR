import unittest
import torch

from run_synthetic_joint_pilot import metrics,aggregate


class SyntheticJointMetricsTests(unittest.TestCase):
    def test_identity_is_no_fake_gain_when_baseline_perfect(self):
        target=torch.ones(1,3,128,128)
        summary=aggregate([dict(metrics=metrics(target,target,target))])
        self.assertEqual(summary['output_mse'],0.)
        self.assertIsNone(summary['mse_ratio']);self.assertIsNone(summary['output_gradient_gain'])

    def test_constant_colour_bias_is_measured(self):
        target=torch.zeros(1,3,128,128);baseline=target+.1;output=target+.05
        summary=aggregate([dict(metrics=metrics(target,baseline,output))])
        self.assertAlmostEqual(summary['mse_ratio'],.25,places=6)
        torch.testing.assert_close(torch.tensor(summary['channel_bias']),torch.full((3,),.05))

    def test_gradient_attenuation_visible_despite_reduced_noise_error(self):
        axis=torch.linspace(0,1,128);target=axis[None,None,None].expand(1,3,128,128).clone()
        summary=aggregate([dict(metrics=metrics(target,target,target*.5))])
        self.assertAlmostEqual(summary['output_gradient_gain'],.5,places=5)

    def test_invalid_support_and_nonfinite_refused(self):
        target=torch.zeros(1,3,32,32)
        with self.assertRaises(ValueError):metrics(target,target,target)
        target=torch.zeros(1,3,128,128);target[0,0,0,0]=float('nan')
        with self.assertRaises(ValueError):metrics(target,target,target)


if __name__=='__main__':unittest.main()
