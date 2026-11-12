import unittest

import torch

from polynomial_residual import quadratic_kernel, residual_and_variance


class PolynomialResidualTests(unittest.TestCase):
    def test_kernel_reproduces_quadratics_and_has_expected_noise_leverage(self):
        kernel = quadratic_kernel()
        y, x = torch.meshgrid(torch.arange(-2, 3, dtype=torch.float64), torch.arange(-2, 3, dtype=torch.float64), indexing='ij')
        self.assertAlmostEqual(float(kernel.sum()), 1., places=14)
        for monomial in (x, y, x*y, x.square(), y.square()):
            self.assertAlmostEqual(float((kernel*monomial).sum()), 0., places=14)
        self.assertAlmostEqual(float(kernel.square().sum()), 27/175, places=14)

    def test_residual_removes_quadratic_signal_and_preserves_transpose(self):
        y, x = torch.meshgrid(torch.linspace(-1, 1, 32), torch.linspace(-1, 1, 40), indexing='ij')
        packed = (.2+.1*x-.15*y+.2*x*x+.12*x*y-.1*y*y)[None, None].expand(1, 4, -1, -1).contiguous()
        sigma = torch.full_like(packed, .02)
        residual, variance = residual_and_variance(packed, sigma)
        self.assertLess(float(residual[:, :, 2:-2, 2:-2].abs().max()), 1e-7)
        self.assertTrue(torch.allclose(variance, sigma.square()*(148/175), atol=1e-10))
        transposed, _ = residual_and_variance(packed[:, [0, 2, 1, 3]].transpose(-2, -1), sigma[:, [0, 2, 1, 3]].transpose(-2, -1))
        self.assertTrue(torch.equal(transposed, residual[:, [0, 2, 1, 3]].transpose(-2, -1)))


if __name__ == '__main__':
    unittest.main()
