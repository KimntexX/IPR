import unittest
import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from bayer_canonical import predict
from closed_moment_unet import ClosedMomentUNet
from local_moment_unet import LocalMomentUNet, local_descriptors, LOCAL_PERMUTATION
from joint_bayer_unet import sample_rgb, bilinear
from synthetic_bayer_data import PATTERNS


class LocalMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_descriptors_are_local_constant_zero_and_transpose_covariant(self):
        packed = torch.rand(1, 4, 16, 20)*1.7-.3
        before = packed.clone()
        features = local_descriptors(packed)
        transposed = local_descriptors(packed[:, (0, 2, 1, 3)].transpose(-2, -1))
        torch.testing.assert_close(transposed, features[:, LOCAL_PERMUTATION].transpose(-2, -1))
        self.assertFalse(local_descriptors(torch.ones_like(packed)*1.3).any())
        self.assertTrue(torch.equal(packed, before))
        changed = packed.clone()
        changed[:, :, 8, 10] += .4
        difference = (local_descriptors(changed)-features).abs()
        self.assertFalse(difference[:, :, :7].any())

    def test_parent_initialization_and_bilinear_start_are_preserved(self):
        torch.manual_seed(71)
        baseline = ClosedMomentUNet(8, True)
        torch.manual_seed(71)
        local = LocalMomentUNet(8)
        self.assertTrue(all(torch.equal(v, local.state_dict()[n]) for n, v in baseline.state_dict().items()))
        self.assertEqual(sum(p.numel() for p in local.parameters()), 33368)
        truth = torch.rand(1, 3, 32, 40)
        tags = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(truth, tags)
        torch.testing.assert_close(local(packed, torch.zeros_like(packed), tags, True), bilinear(packed, tags))

    def test_nonzero_short_route_preserves_d4_and_has_finite_gradients(self):
        torch.manual_seed(117)
        model = LocalMomentUNet(4)
        torch.nn.init.uniform_(model.local_head.weight, -.1, .1)
        torch.nn.init.uniform_(model.head.weight, -.1, .1)
        truth = torch.rand(1, 3, 32, 40)*1.7-.3
        tags = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(truth, tags)
        output = predict(model, packed, torch.zeros_like(packed), tags, True, True)
        for op in OPERATIONS:
            tp, labels = transform_observation(packed, tags, op)
            image = transform(predict(model, tp, torch.zeros_like(tp), labels, True, True), op, True)
            torch.testing.assert_close(image, output, rtol=3e-6, atol=2e-6)
            self.assertTrue(torch.equal(sample_rgb(image, tags), packed))
        output.square().mean().backward()
        self.assertTrue(all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()))
        self.assertGreater(float(model.local_features[0].weight.grad.abs().sum()), 0.)

    def test_invalid_descriptors_and_noisy_stage_rejected(self):
        for value in (None, torch.zeros(1, 3, 16, 16), torch.full((1, 4, 16, 16), float('nan'))):
            with self.assertRaises(ValueError):
                local_descriptors(value)
        p = torch.tensor([PATTERNS[0]])
        packed = torch.rand(1, 4, 16, 16)
        with self.assertRaises(ValueError):
            LocalMomentUNet(4)(packed, torch.ones_like(packed), p, True)


if __name__ == '__main__':
    unittest.main()
