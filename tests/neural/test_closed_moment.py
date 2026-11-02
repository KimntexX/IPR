import unittest
import torch

from audit_bayer_symmetries import transform_observation
from closed_moment_operator import CLOSED_STENCILS, CHANNEL_COUNT, TRANSPOSE_PERMUTATION, interpolate_closed, transpose_coefficients
from joint_bayer_unet import sample_rgb, bilinear
from synthetic_bayer_data import PATTERNS


class ClosedMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_basis_closure_same_colour_moments_and_channel_bijection(self):
        self.assertEqual(CHANNEL_COUNT, 36)
        self.assertEqual(sorted(TRANSPOSE_PERMUTATION), list(range(36)))
        self.assertTrue(all(TRANSPOSE_PERMUTATION[TRANSPOSE_PERMUTATION[i]] == i for i in range(36)))
        colour_pattern = ((0, 1), (1, 2))
        for colour, y, x, supports in CLOSED_STENCILS:
            self.assertEqual(len(supports), 6 if colour == 1 else 4)
            for support in supports:
                self.assertEqual(sum(weight for _, _, weight in support), 1.)
                self.assertEqual(sum(dy*weight for dy, _, weight in support), 0.)
                self.assertEqual(sum(dx*weight for _, dx, weight in support), 0.)
                for dy, dx, _ in support:
                    self.assertEqual(colour_pattern[(y+dy) % 2][(x+dx) % 2], colour)
                    self.assertLessEqual(max(abs(dy), abs(dx)), 3)

    def test_arbitrary_coefficients_transform_covariantly_without_source_changes(self):
        g = torch.Generator().manual_seed(104)
        truth = torch.rand(1, 3, 32, 40, generator=g)*1.7-.3
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(truth, pattern)
        before = packed.clone()
        coeff = torch.rand(1, 36, 16, 20, generator=g)*2-1
        output = interpolate_closed(packed, pattern, coeff)
        tp, tags = transform_observation(packed, pattern, (3, True))
        transposed = interpolate_closed(tp, tags, transpose_coefficients(coeff))
        torch.testing.assert_close(transposed.transpose(-2, -1), output, rtol=2e-6, atol=2e-6)
        self.assertTrue(torch.equal(transpose_coefficients(transpose_coefficients(coeff)), coeff))
        self.assertTrue(torch.equal(sample_rgb(output, pattern), packed))
        self.assertTrue(torch.equal(packed, before))

    def test_constants_affine_and_initial_bilinear(self):
        y, x = torch.meshgrid(torch.arange(32)/32, torch.arange(40)/40, indexing='ij')
        targets = (torch.tensor([-.2, .3, 1.3])[None, :, None, None].expand(1, 3, 32, 40),
            torch.stack((-.2+x*.3, .4-y*.2, 1.2+x*.1-y*.1))[None])
        p = torch.tensor([PATTERNS[0]])
        coeff = torch.rand(1, 36, 16, 20)*2-1
        for target in targets:
            packed = sample_rgb(target, p)
            out = interpolate_closed(packed, p, coeff)
            torch.testing.assert_close(out[:, :, 4:-4, 4:-4], target[:, :, 4:-4, 4:-4], rtol=2e-6, atol=5e-7)
            torch.testing.assert_close(interpolate_closed(packed, p, torch.zeros_like(coeff)), bilinear(packed, p))

    def test_gradients_and_invalid_coefficients(self):
        target = torch.rand(1, 3, 32, 40)
        p = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, p)
        coeff = torch.zeros(1, 36, 16, 20, requires_grad=True)
        interpolate_closed(packed, p, coeff).square().mean().backward()
        self.assertTrue(torch.isfinite(coeff.grad).all())
        self.assertGreater(float(coeff.grad.abs().sum()), 0.)
        for bad in (coeff[:, :32], torch.full_like(coeff, float('nan')), torch.full_like(coeff, 1.01)):
            with self.assertRaises(ValueError):
                interpolate_closed(packed, p, bad)


if __name__ == '__main__':
    unittest.main()
