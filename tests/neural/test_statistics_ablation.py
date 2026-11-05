import unittest

import torch

from diagnose_statistics_branch import without_statistics
from noisy_moment_unet import NoisyMomentUNet


class StatisticsAblationTests(unittest.TestCase):
    def test_only_statistics_output_zeroed_and_hook_restored_after_error(self):
        torch.set_num_threads(2)
        model = NoisyMomentUNet(4)
        layer = model.noise_features.statistics
        with torch.no_grad():
            layer.bias.fill_(.5)
        value = torch.randn(1, 32, 8, 8)
        before = {k:v.clone() for k,v in model.state_dict().items()}
        expected = layer(value).detach().clone()
        self.assertGreater(float(expected.abs().max()), 0.)
        with self.assertRaisesRegex(RuntimeError, 'probe failed'):
            with without_statistics(model):
                self.assertTrue(torch.equal(layer(value), torch.zeros_like(expected)))
                raise RuntimeError('probe failed')
        self.assertFalse(layer._forward_hooks)
        self.assertTrue(torch.equal(layer(value), expected))
        self.assertTrue(all(torch.equal(before[k], v) for k,v in model.state_dict().items()))
