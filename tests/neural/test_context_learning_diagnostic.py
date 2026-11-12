import unittest

import torch

from diagnose_context_learning import probe
from joint_bayer_unet import sample_rgb
from noisy_moment_unet import NoisyMomentUNet
from noisy_training_policy import configure_trainable


class LearningDiagnosticTests(unittest.TestCase):
    def test_read_only_probe_identifies_zero_head_without_leaking_hooks(self):
        torch.set_num_threads(2)
        torch.manual_seed(71)
        model = NoisyMomentUNet(4).eval()
        configure_trainable(model, 'cfa_front')
        labels = torch.tensor([[0, 1, 3, 2]])
        truth = torch.full((1, 3, 64, 64), .2)
        clean = sample_rgb(truth, labels)
        sigma = torch.full_like(clean, .01)
        values = clean+torch.randn_like(clean)*sigma
        result = probe(model, values, sigma, labels, truth, 16)
        self.assertEqual(result['measures']['cfa_mse_ratio'], 1.)
        self.assertEqual(result['measures']['clean_anchor_loss'], 0.)
        self.assertGreater(result['component_gradient_norms']['rgb'], 0.)
        self.assertEqual(result['component_gradient_norms']['anchor'], 0.)
        self.assertEqual(result['component_gradient_norms']['structure'], 0.)
        self.assertIsNone(result['component_gradient_cosines']['rgb_structure'])
        self.assertEqual(result['statistics_gradient_norms']['rgb'], 0.)
        self.assertIsNone(result['statistics_gradient_cosines']['rgb_structure'])
        self.assertGreater(result['gradients']['noise_head.weight']['rms'], 0.)
        self.assertEqual(result['gradients']['noise_features.enc1.0.weight']['rms'], 0.)
        self.assertTrue(all(not m._forward_hooks for m in model.modules()))
        self.assertTrue(all(p.grad is None for p in model.parameters()))

    def test_statistics_gradient_is_reported_as_subset_without_weight_updates(self):
        torch.set_num_threads(2)
        torch.manual_seed(72)
        model = NoisyMomentUNet(4).eval()
        configure_trainable(model, 'cfa_front')
        with torch.no_grad():
            model.noise_head.weight.normal_(0., .05)
        labels = torch.tensor([[0, 1, 3, 2]])
        axis = torch.arange(64, dtype=torch.float32)
        truth = (.2+.03*(axis[None,:]+axis[:,None]).sin())[None,None].expand(1,3,64,64).clone()
        clean = sample_rgb(truth, labels)
        sigma = torch.full_like(clean, .01)
        values = clean+torch.randn_like(clean)*sigma
        before = {k:v.clone() for k,v in model.state_dict().items()}
        result = probe(model, values, sigma, labels, truth, 16, 0.)
        self.assertGreater(result['statistics_gradient_norms']['rgb'], 0.)
        for name, norm in result['statistics_gradient_norms'].items():
            self.assertLessEqual(norm, result['component_gradient_norms'][name]+1e-6)
        for value in result['statistics_gradient_cosines'].values():
            if value is not None:
                self.assertLessEqual(abs(value), 1.+1e-6)
        self.assertTrue(all(torch.equal(before[k],v) for k,v in model.state_dict().items()))
        self.assertTrue(all(not m._forward_hooks for m in model.modules()))
        self.assertTrue(all(p.grad is None for p in model.parameters()))
