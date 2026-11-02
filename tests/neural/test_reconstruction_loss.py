import unittest

import torch

from reconstruction_loss import relative_reconstruction_loss


class ReconstructionLossTests(unittest.TestCase):
    def test_equal_relative_errors_receive_equal_weight_despite_energy(self):
        truth = torch.zeros(2, 3, 4, 6)
        reference = torch.ones_like(truth)
        reference[0] *= .01
        prediction = reference.clone().mul_(.5).requires_grad_()
        loss = relative_reconstruction_loss(prediction, truth, reference, 1, 0)
        self.assertAlmostEqual(float(loss.detach()), .25, places=6)
        loss.backward()
        self.assertTrue(torch.isfinite(prediction.grad).all())
        self.assertGreater(float(prediction.grad[0].abs().mean()), float(prediction.grad[1].abs().mean()))

    def test_signed_scale_equivariance_floor_and_reference_detach(self):
        truth = torch.full((1, 3, 4, 6), -.1, requires_grad=True)
        reference = truth.detach().clone().requires_grad_()
        prediction = (truth.detach()+.001).requires_grad_()
        a = relative_reconstruction_loss(prediction, truth, reference, 1, 0)
        b = relative_reconstruction_loss(prediction/2, truth/2, reference/2, 2, 0)
        self.assertAlmostEqual(float(a.detach()), 1, places=4)
        self.assertEqual(float(a.detach()), float(b.detach()))
        a.backward()
        self.assertIsNone(truth.grad)
        self.assertIsNone(reference.grad)
        self.assertIsNotNone(prediction.grad)

    def test_crop_excludes_border_and_rejects_invalid_data(self):
        target = torch.zeros(1, 3, 6, 6)
        prediction = target.clone()
        prediction[:, :, 0] = 100
        self.assertEqual(float(relative_reconstruction_loss(prediction, target, target, 1, 1)), 0)
        for scale, border in ((0, 1), (float('nan'), 1), (1, 3)):
            with self.assertRaises(ValueError):
                relative_reconstruction_loss(prediction, target, target, scale, border)
        prediction[:, :, 2, 2] = float('inf')
        with self.assertRaises(ValueError):
            relative_reconstruction_loss(prediction, target, target, 1, 1)


if __name__ == '__main__':
    unittest.main()
