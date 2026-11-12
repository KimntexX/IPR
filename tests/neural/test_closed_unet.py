import unittest
import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from bayer_canonical import predict
from closed_moment_unet import ClosedMomentUNet, TransposeConv
from joint_bayer_unet import sample_rgb, bilinear
from moment_bayer_unet import MomentBayerUNet
from synthetic_bayer_data import PATTERNS


class ClosedUNetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_parameter_count_and_unconstrained_trunk_initialization(self):
        torch.manual_seed(71)
        previous = MomentBayerUNet(8)
        torch.manual_seed(71)
        current = ClosedMomentUNet(8)
        self.assertEqual(sum(p.numel() for p in current.parameters()), 31300)
        for name, value in previous.state_dict().items():
            if not name.startswith('head.'):
                self.assertTrue(torch.equal(value, current.state_dict()[name]))

    def test_nonzero_equivariant_model_all_d4_with_non_square_signed_data(self):
        torch.manual_seed(105)
        model = ClosedMomentUNet(4, equivariant=True)
        torch.nn.init.uniform_(model.head.weight, -.1, .1)
        torch.nn.init.uniform_(model.head.bias, -.1, .1)
        target = torch.rand(1, 3, 32, 40)*1.7-.3
        p = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, p)
        output = predict(model, packed, torch.zeros_like(packed), p, True, True)
        for op in OPERATIONS:
            tp, tags = transform_observation(packed, p, op)
            image = transform(predict(model, tp, torch.zeros_like(tp), tags, True, True), op, inverse=True)
            torch.testing.assert_close(image, output, rtol=3e-6, atol=2e-6)
            self.assertTrue(torch.equal(sample_rgb(image, p), packed))
        output.square().mean().backward()
        self.assertTrue(all(v.grad is None or torch.isfinite(v.grad).all() for v in model.parameters()))
        self.assertGreater(float(model.head.weight.grad.abs().sum()), 0.)

    def test_zero_head_is_bilinear_and_noisy_stage_is_rejected(self):
        target = torch.rand(1, 3, 32, 40)
        p = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, p)
        for equivariant in (False, True):
            model = ClosedMomentUNet(4, equivariant)
            torch.testing.assert_close(model(packed, torch.zeros_like(packed), p, True), bilinear(packed, p))
            with self.assertRaises(ValueError):
                model(packed, torch.ones_like(packed)*.01, p, True)

    def test_invalid_width_and_channel_representation_rejected(self):
        with self.assertRaises(ValueError):
            ClosedMomentUNet(5)
        with self.assertRaises(ValueError):
            TransposeConv((1, 1), (0, 1))


if __name__ == '__main__':
    unittest.main()
