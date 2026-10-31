"""Small CPU contract tests, NOT a photographic denoising qualification."""
import unittest

import torch
from torch import nn

from camera_unet import CameraUNet, Cancelled, apply, validate


class CameraUNetTests(unittest.TestCase):
    def setUp(self):
        torch.manual_seed(41)
        self.signal = torch.linspace(-.2, 1.5, 3*17*19).reshape(1, 3, 17, 19)
        self.sigma = torch.full_like(self.signal, .03)
        self.model = CameraUNet().eval()

    def test_initial_identity_and_extended_range(self):
        result = apply(self.model, self.signal, self.sigma, 100)
        self.assertTrue(torch.equal(result, self.signal))
        self.assertLess(result.min().item(), 0)
        self.assertGreater(result.max().item(), 1)

    def test_zero_never_calls_model(self):
        class Bomb(nn.Module):
            def forward(self, *args):
                raise AssertionError('zero called the network')
        self.assertIs(apply(Bomb().eval(), self.signal, self.sigma, 0), self.signal)

    def test_no_source_mutation_and_signed_residual(self):
        before = self.signal.clone()
        with torch.no_grad():
            self.model.head.bias.fill_(.1)
        result = apply(self.model, self.signal, self.sigma, 50)
        self.assertTrue(torch.equal(before, self.signal))
        self.assertTrue(torch.allclose(result, before-.05, atol=2e-7, rtol=0))
        self.assertNotEqual(result.data_ptr(), self.signal.data_ptr())

    def test_shape_and_batch(self):
        for h, w in ((8, 8), (17, 19), (32, 48)):
            x = torch.zeros((2, 3, h, w))
            self.assertEqual(self.model(x, torch.zeros_like(x)).shape, x.shape)

    def test_invalid_shapes(self):
        for shape in ((3, 8, 8), (0, 3, 8, 8), (1, 4, 8, 8), (1, 3, 7, 8)):
            x = torch.zeros(shape)
            with self.assertRaises(ValueError):
                validate(x, torch.zeros_like(x))
        with self.assertRaises(ValueError):
            validate(self.signal, self.sigma[:, :1])

    def test_invalid_types_and_dtype(self):
        for x, s in ((None, self.sigma), (self.signal.double(), self.sigma),
                     (self.signal, self.sigma.double())):
            with self.assertRaises(ValueError):
                validate(x, s)

    def test_nonfinite_at_zero_also_rejected(self):
        for value in (float('nan'), float('inf'), -float('inf')):
            x = self.signal.clone()
            x[0, 0, 0, 0] = value
            with self.assertRaises(ValueError):
                apply(self.model, x, self.sigma, 0)

    def test_invalid_sigma(self):
        for value in (-.01, float('nan'), float('inf')):
            sigma = self.sigma.clone()
            sigma[0, 0, 0, 0] = value
            with self.assertRaises(ValueError):
                validate(self.signal, sigma)

    def test_invalid_amount(self):
        for value in (-1, 101, True, '50', float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                apply(self.model, self.signal, self.sigma, value)

    def test_cancellation_before_and_after(self):
        before = self.signal.clone()
        with self.assertRaises(Cancelled):
            apply(self.model, self.signal, self.sigma, 100, lambda: True)
        calls = iter((False, True))
        with self.assertRaises(Cancelled):
            apply(self.model, self.signal, self.sigma, 100, lambda: next(calls))
        self.assertTrue(torch.equal(before, self.signal))

    def test_invalid_model_result(self):
        for kind in ('shape', 'nan', 'none'):
            class Broken(nn.Module):
                def forward(inner, x, sigma):
                    if kind == 'none':
                        return None
                    return x[:, :1] if kind == 'shape' else x*float('nan')
            with self.assertRaises(ValueError):
                apply(Broken().eval(), self.signal, self.sigma, 100)

    def test_training_model_rejected_for_apply(self):
        with self.assertRaises(ValueError):
            apply(self.model.train(), self.signal, self.sigma, 100)

    def test_noise_conditioning_reaches_first_layer(self):
        seen = []
        hook = self.model.enc1[0].register_forward_pre_hook(
            lambda module, args: seen.append(args[0].detach().clone()))
        try:
            self.model(self.signal, self.sigma)
        finally:
            hook.remove()
        self.assertTrue(torch.equal(seen[0][:, :3], self.signal))
        self.assertTrue(torch.equal(seen[0][:, 3:], self.sigma))

    def test_gradient_and_tiny_optimization(self):
        # One fixed synthetic patch: checks optimizer wiring only, NOT generalization.
        model = self.model.train()
        target = torch.zeros((1, 3, 16, 16))
        noisy = target + .04*torch.randn_like(target)
        sigma = torch.full_like(noisy, .04)
        optimizer = torch.optim.Adam(model.parameters(), lr=.003)
        losses = []
        for _ in range(16):
            optimizer.zero_grad(set_to_none=True)
            loss = (model(noisy, sigma)-target).square().mean()
            loss.backward()
            self.assertTrue(all(p.grad is None or torch.isfinite(p.grad).all()
                                for p in model.parameters()))
            optimizer.step()
            losses.append(loss.item())
        self.assertLess(losses[-1], losses[0])


if __name__ == '__main__':
    torch.set_num_threads(2)
    torch.set_num_interop_threads(1)
    unittest.main()
