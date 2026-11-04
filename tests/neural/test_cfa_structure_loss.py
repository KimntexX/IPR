import unittest

import torch

from cfa_structure_loss import structure_loss


class CfaStructureLossTests(unittest.TestCase):
    def test_identity_and_constant_targets_have_zero_loss_and_gradient(self):
        for clean in (torch.full((1, 4, 16, 16), .2), torch.rand(1, 4, 16, 16)):
            corrected = clean.clone().requires_grad_(True)
            loss = structure_loss(corrected, clean, torch.full_like(clean, .01), 2)
            loss.backward()
            self.assertEqual(float(loss.detach()), 0.)
            self.assertTrue(torch.equal(corrected.grad, torch.zeros_like(clean)))

    def test_penalizes_coherent_shrink_more_than_orthogonal_noise(self):
        clean = torch.linspace(-.1, .1, 16).view(1, 1, 1, 16).expand(1, 4, 16, 16).clone()
        sigma = torch.full_like(clean, .02)
        corrected = (clean*.8).requires_grad_(True)
        loss = structure_loss(corrected, clean, sigma, 0)
        loss.backward()
        self.assertGreater(float(loss.detach()), 50.)
        self.assertLess(float((corrected.grad*clean).sum()), 0.)
        noise = torch.tensor([1., -1.]).repeat(8).view(1, 1, 16, 1)*.02
        self.assertLess(float(structure_loss(clean+noise, clean, sigma, 0)), 1e-10)

    def test_scale_and_channel_permutation_invariant_and_targets_detached(self):
        torch.manual_seed(741)
        clean = torch.rand(1, 4, 16, 16, requires_grad=True)
        sigma = torch.full_like(clean, .02, requires_grad=True)
        corrected = (clean.detach()*.95).requires_grad_(True)
        loss = structure_loss(corrected, clean, sigma, 2)
        perm = [2, 0, 3, 1]
        other = structure_loss(corrected[:, perm]/2, clean[:, perm]/2, sigma[:, perm]/2, 2)
        self.assertAlmostEqual(float(loss.detach()), float(other.detach()), places=4)
        loss.backward()
        self.assertIsNone(clean.grad)
        self.assertIsNone(sigma.grad)

    def test_independent_noise_has_unit_expected_projection_and_rejects_invalid_data(self):
        torch.manual_seed(742)
        clean = torch.linspace(-.1, .1, 16).view(1, 1, 1, 16).expand(512, 4, 16, 16).clone()
        sigma = torch.full_like(clean, .02)
        loss = structure_loss(clean+torch.randn_like(clean)*sigma, clean, sigma, 0)
        self.assertLess(abs(float(loss)-1), .1)
        with self.assertRaises(ValueError):
            structure_loss(clean, clean, -sigma, 0)
        self.assertEqual(float(structure_loss(clean, clean, torch.zeros_like(clean), 0)), 0.)
