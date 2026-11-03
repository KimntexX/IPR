import unittest

import torch

from clean_cfa_loss import clean_cfa_loss


class CleanCfaLossTests(unittest.TestCase):
    def test_physical_scale_invariance_zero_anchor_and_gradient(self):
        clean = torch.full((1, 4, 64, 64), .3)
        target = torch.full((1, 3, 128, 128), .3)
        reference = target+.01
        corrected = (clean+.002).requires_grad_()
        loss = clean_cfa_loss(corrected, clean, reference, target, 1.)
        self.assertGreater(float(loss.detach()), 0.)
        scaled = clean_cfa_loss(corrected/2, clean/2, reference/2, target/2, 2.)
        torch.testing.assert_close(loss, scaled, atol=1e-7, rtol=1e-6)
        self.assertEqual(float(clean_cfa_loss(clean, clean, reference, target, 1.)), 0.)
        loss.backward()
        self.assertTrue(torch.isfinite(corrected.grad).all())
        self.assertGreater(float(corrected.grad[:, :, 24:-24, 24:-24].abs().sum()), 0.)
        self.assertEqual(float(corrected.grad[:, :, :24].abs().sum()), 0.)

    def test_reference_is_detached_and_invalid_arrays_are_rejected(self):
        clean = torch.zeros(1, 4, 64, 64, requires_grad=True)
        corrected = torch.ones_like(clean, requires_grad=True)*.001
        target = torch.zeros(1, 3, 128, 128, requires_grad=True)
        reference = torch.ones_like(target, requires_grad=True)*.01
        clean_cfa_loss(corrected, clean, reference, target, 1.).backward()
        self.assertIsNone(clean.grad)
        self.assertIsNone(target.grad)
        with self.assertRaises(ValueError):
            clean_cfa_loss(corrected, clean, reference, target, 1., border=47)
        with self.assertRaises(ValueError):
            clean_cfa_loss(corrected, clean, reference, target, float('inf'))
        with self.assertRaises(ValueError):
            clean_cfa_loss(corrected*float('nan'), clean, reference, target, 1.)


if __name__ == '__main__':
    unittest.main()
