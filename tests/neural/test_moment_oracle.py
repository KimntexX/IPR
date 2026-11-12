import unittest
import torch

from audit_moment_oracle import box_witness, oracle
from joint_bayer_unet import sample_rgb, bilinear
from synthetic_bayer_data import PATTERNS


class MomentOracleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_interval_projection_and_zero_support(self):
        d = torch.tensor([[[1., -1., 0., 1.], [-2., 2., 0., -2.],
                           [0., 0., 0., 0.], [1., 1., 0., 1.]]])
        t = torch.tensor([[2., -9., 5., 9.]])
        coeff, optimum, radius = box_witness(d, t)
        self.assertTrue(torch.equal(radius, torch.tensor([[4., 4., 0., 4.]])))
        self.assertTrue(torch.equal(optimum, torch.tensor([[2., -4., 0., 4.]])))
        torch.testing.assert_close((coeff*d).sum(1), optimum)
        self.assertLessEqual(float(coeff.abs().max()), 1.)
        self.assertTrue(torch.equal(coeff[:, :, 2], torch.zeros(1, 4)))

    def test_optimum_beats_box_corners_and_preserves_scale(self):
        g = torch.Generator().manual_seed(91)
        d = torch.randn(2, 4, 30, generator=g)
        t = torch.randn(2, 30, generator=g)*3
        coeff, optimum, _ = box_witness(d, t)
        loss = (optimum-t).square()
        for bits in range(16):
            corner = torch.tensor([1. if bits & (1 << i) else -1. for i in range(4)])[None, :, None]
            self.assertTrue((loss <= ((d*corner).sum(1)-t).square()+1e-5).all())
        scaled, value, _ = box_witness(d/2, t/2)
        torch.testing.assert_close(scaled, coeff)
        torch.testing.assert_close(value*2, optimum)

    def test_witness_uses_actual_interpolator_and_copies_observations(self):
        g = torch.Generator().manual_seed(92)
        truth = torch.rand(1, 3, 32, 40, generator=g)*1.4-.2
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(truth, pattern)
        before = packed.clone()
        result, _, _, _ = oracle(packed, pattern, truth)
        base = bilinear(packed, pattern)
        self.assertLessEqual(float((result-truth).square().mean()), float((base-truth).square().mean())+1e-10)
        self.assertTrue(torch.equal(sample_rgb(result, pattern), packed))
        self.assertTrue(torch.equal(packed, before))

    def test_invalid_and_nonfinite_inputs_rejected(self):
        d = torch.ones(1, 4, 5)
        t = torch.ones(1, 5)
        for bad in (d[:, :3], d.double(), d*float('nan'), d*float('inf')):
            with self.assertRaises(ValueError):
                box_witness(bad, t)
        with self.assertRaises(ValueError):
            box_witness(d, t*float('nan'))


if __name__ == '__main__':
    unittest.main()
