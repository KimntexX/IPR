import unittest

import torch

from noise_conditioning import standardized_residual


class NoiseConditioningTests(unittest.TestCase):
    def test_flat_and_affine_zero_scale_equivariance_and_no_source_mutation(self):
        y, x = torch.meshgrid(torch.linspace(-1, 1, 32), torch.linspace(-1, 1, 40), indexing='ij')
        packed = (.3*x-.2*y-.1)[None, None].expand(1, 4, -1, -1).contiguous()
        before = packed.clone()
        sigma = torch.full_like(packed, .02)
        result = standardized_residual(packed, sigma)
        self.assertLess(float(result[:, :, 2:-2, 2:-2].abs().max()), 1e-5)
        self.assertTrue(torch.equal(packed, before))
        self.assertTrue(torch.equal(result, standardized_residual(packed/2, sigma/2)))
        self.assertTrue((standardized_residual(packed, torch.zeros_like(sigma)) == 0).all())

    def test_gaussian_noise_has_unit_residual_scale_and_features_are_bounded(self):
        packed = torch.randn(1, 4, 64, 64, generator=torch.Generator().manual_seed(704))*.02+.3
        sigma = torch.full_like(packed, .02)
        residual = standardized_residual(packed, sigma)[:, :, 2:-2, 2:-2]
        self.assertAlmostEqual(float(residual.square().mean().sqrt()), 1., delta=.05)
        self.assertLess(abs(float(residual.mean())), .03)
        packed[:, :, 32, 32] += 2
        result = standardized_residual(packed, sigma)
        self.assertTrue((result.abs() <= 8).all())
        self.assertTrue((result[:, :, 32, 32] == 8).all())

    def test_transpose_covariance_and_invalid_sigma(self):
        packed = torch.randn(1, 4, 24, 32, generator=torch.Generator().manual_seed(705))
        sigma = torch.full_like(packed, .02)
        expected = standardized_residual(packed, sigma)[:, [0, 2, 1, 3]].transpose(-2, -1)
        actual = standardized_residual(packed[:, [0, 2, 1, 3]].transpose(-2, -1), sigma[:, [0, 2, 1, 3]].transpose(-2, -1))
        self.assertTrue(torch.equal(actual, expected))
        with self.assertRaises(ValueError):
            standardized_residual(packed, -sigma)


if __name__ == '__main__':
    unittest.main()
