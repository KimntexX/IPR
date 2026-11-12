import unittest

import torch

from cfa_structure_loss import structure_loss


class CfaStructureLossTests(unittest.TestCase):
    def test_continuous_transition_matches_single_projection_and_dual_endpoint(self):
        torch.manual_seed(751)
        clean = torch.rand(1, 4, 16, 16)
        sigma = torch.full_like(clean, .02)
        corrected = clean*.95
        a = structure_loss(corrected, clean, sigma, 2, 0.)
        b = structure_loss(corrected, clean, sigma, 2, 1.)
        halfway = structure_loss(corrected, clean, sigma, 2, .5)
        target = clean[:, :, 2:-2, 2:-2]
        detail = target-target[:, :, :1, :1]
        detail = detail-detail.mean(dim=(-2, -1), keepdim=True)
        expected = (((corrected[:, :, 2:-2, 2:-2]-target)*detail).sum(dim=(-2, -1)).square()
            /(detail.square()*sigma[:, :, 2:-2, 2:-2].square()).sum(dim=(-2, -1))).mean()
        self.assertAlmostEqual(float(a), float(expected), places=4)
        self.assertAlmostEqual(float(halfway), float((a+b)/2), places=4)
        with self.assertRaises(ValueError):
            structure_loss(corrected, clean, sigma, 2, -1)

    def test_two_noise_orthogonal_projections_keep_unit_noise_scale(self):
        torch.manual_seed(743)
        x = torch.arange(32, dtype=torch.float32)*(2*torch.pi/32)
        clean = (.2+.04*x.sin()+.02*(5*x).sin()).view(1, 1, 1, 32).expand(256, 4, 32, 32).clone()
        sigma = (.01+.01*x.cos().square()).view(1, 1, 1, 32).expand_as(clean).clone()
        corrected = clean+torch.randn_like(clean)*sigma
        loss = structure_loss(corrected, clean, sigma, 2)
        self.assertLess(abs(float(loss)-1.), .1)
        transposed = structure_loss(corrected.transpose(-2, -1), clean.transpose(-2, -1), sigma.transpose(-2, -1), 2)
        self.assertAlmostEqual(float(loss), float(transposed), places=5)

    def test_two_components_cannot_hide_behind_single_global_projection(self):
        x = torch.arange(32, dtype=torch.float32)*(2*torch.pi/32)
        low, high = .04*x.sin(), .04*(5*x).sin()
        clean = (.2+low+high).view(1, 1, 1, 32).expand(1, 4, 32, 32).clone()
        corrected = clean+(.2*low-.2*high).view(1, 1, 1, 32)
        sigma = torch.full_like(clean, .02)
        detail = clean-clean.mean(dim=(-2, -1), keepdim=True)
        single = (((corrected-clean)*detail).sum(dim=(-2, -1)).square()
            /(detail.square()*sigma.square()).sum(dim=(-2, -1))).mean()
        self.assertLess(float(single), 1e-8)
        self.assertGreater(float(structure_loss(corrected, clean, sigma, 0)), 10.)

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
        loss = structure_loss(corrected, clean, sigma, 2)
        loss.backward()
        detail = clean[:, :, 2:-2, 2:-2]
        detail = detail-detail.mean(dim=(-2, -1), keepdim=True)
        expected = (((corrected.detach()-clean)[:, :, 2:-2, 2:-2]*detail).sum(dim=(-2, -1)).square()
            /(detail.square()*sigma[:, :, 2:-2, 2:-2].square()).sum(dim=(-2, -1))).mean()
        self.assertGreater(float(loss.detach()), 0.)
        self.assertAlmostEqual(float(loss.detach()), float(expected), places=3)
        self.assertLess(float((corrected.grad*clean).sum()), 0.)
        noise = torch.tensor([1., -1.]).repeat(8).view(1, 1, 16, 1)*.02
        self.assertLess(float(structure_loss(clean+noise, clean, sigma, 2)), 1e-10)

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
