import unittest

import torch

from cfa_correlation import correlation_features
from study_cfa_correlation import auc


class CfaCorrelationTests(unittest.TestCase):
    def test_auc_ties_and_perfect_and_reversed_separation(self):
        self.assertEqual(auc([0.,0.],[0.,0.]), .5)
        self.assertEqual(auc([2.,3.],[0.,1.]), 1.)
        self.assertEqual(auc([0.,1.],[2.,3.]), 0.)
        with self.assertRaises(ValueError):
            auc([], [0.])
    def test_flat_independent_noise_has_zero_signed_mean_with_variable_variance(self):
        torch.set_num_threads(2)
        torch.manual_seed(760)
        sigma = torch.linspace(.01, .03, 40).reshape(1, 1, 1, 40).expand(256, 4, 40, 40).clone()
        values = .2+torch.randn_like(sigma)*sigma
        features = correlation_features(values, sigma, 9)[:, :, :, 10:-10, 10:-10]
        self.assertLess(float(features.mean(dim=(0, 3, 4)).abs().max()), .02)

    def test_affine_signal_analytic_value_and_spatial_transpose(self):
        x = torch.arange(40, dtype=torch.float32)
        values = (.2+.01*x[None, :]+.02*x[:, None])[None, None].expand(1, 4, 40, 40).clone()
        sigma = torch.full_like(values, .02)
        f = correlation_features(values, sigma, 9)
        core = f[:, :, :, 10:-10, 10:-10]
        self.assertTrue(torch.allclose(core[:, :, 0], torch.full_like(core[:, :, 0], .5), atol=2e-6))
        # Input here is noiseless but sigma is supplied: lag2 contains the explicit covariance correction.
        self.assertTrue(torch.allclose(core[:, :, 1], torch.ones_like(core[:, :, 1]), atol=2e-6))
        t = correlation_features(values.transpose(-2, -1), sigma.transpose(-2, -1), 9)
        self.assertTrue(torch.allclose(f[:, :, [2, 3, 0, 1]].transpose(-2, -1), t, atol=2e-5))

    def test_zero_sigma_and_scale_invariance_and_invalid_input(self):
        torch.manual_seed(761)
        values = torch.rand(1, 4, 40, 40)
        sigma = torch.full_like(values, .02)
        self.assertTrue(torch.equal(correlation_features(values, torch.zeros_like(sigma), 9), torch.zeros(1, 4, 4, 40, 40)))
        self.assertTrue(torch.allclose(correlation_features(values, sigma, 9),
            correlation_features(values/2, sigma/2, 9), atol=1e-6))
        with self.assertRaises(ValueError):
            correlation_features(values, sigma, 8)
