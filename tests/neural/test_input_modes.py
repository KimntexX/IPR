import unittest

import torch
from torch import nn

from input_modes import ANSCOMBE_INPUT_SCALE, ModeCorrector, algebraic_inverse, anscombe, in_channels, mode_loss, oracle_alpha


class ZeroCore(nn.Module):
    """Stand-in core: records its input, predicts no correction."""

    def __init__(self, channels):
        super().__init__()
        self.seen = None
        self.channels = channels

    def forward(self, values):
        if values.shape[1] != self.channels:
            raise ValueError('Unexpected input channels')
        self.seen = values
        return torch.zeros(values.shape[0], 4, *values.shape[-2:], dtype=values.dtype)


def law(batch=2):
    a = torch.tensor([5e-4, 6e-4, 6e-4, 5.5e-4]).repeat(batch, 1)[:, :, None, None]
    b = torch.tensor([2e-6, 3e-6, 3e-6, 2.5e-6]).repeat(batch, 1)[:, :, None, None]
    return a, b


class InputModeTests(unittest.TestCase):
    def test_anscombe_round_trip_and_unit_noise(self):
        a, b = law(1)
        y = torch.linspace(-.05, .9, 64).repeat(1, 4, 64, 1)
        torch.testing.assert_close(algebraic_inverse(anscombe(y, a, b), a, b), y, rtol=1e-5, atol=1e-7)
        generator = torch.Generator().manual_seed(3)
        clean = torch.full((1, 4, 256, 256), .2, dtype=torch.float64)
        photons = torch.poisson((clean/a).expand(1, 4, 256, 256).contiguous(), generator=generator)
        noisy = a*photons+b.sqrt()*torch.randn(clean.shape, generator=generator, dtype=torch.float64)
        spread = anscombe(noisy, a.double(), b.double()).std(dim=(-2, -1))
        torch.testing.assert_close(spread, torch.ones_like(spread), rtol=.02, atol=0)

    def test_each_mode_feeds_its_own_input_and_returns_the_identity_without_correction(self):
        a, b = law()
        noisy = torch.rand(2, 4, 16, 16)*.5-.02
        sigma = (a*noisy.clamp_min(0)+b).sqrt()
        tags = torch.tensor([[0, 1, 3, 2]]*2)
        for mode in ('sigma_map', 'blind', 'anscombe'):
            core = ZeroCore(in_channels(mode))
            output = ModeCorrector(core, mode).correct(noisy, sigma, tags, a, b)
            torch.testing.assert_close(output['corrected'], noisy, rtol=1e-5, atol=1e-6)
            self.assertEqual(output['z_hat'] is None, mode != 'anscombe')
        torch.testing.assert_close(core.seen, ANSCOMBE_INPUT_SCALE*anscombe(noisy, a, b))
        with self.assertRaises(ValueError):
            ModeCorrector(ZeroCore(4), 'denoise_harder')
        with self.assertRaises(ValueError):
            ModeCorrector(ZeroCore(4), 'blind').correct(noisy, sigma, tags, -a, b)

    def test_losses_pair_with_their_mode(self):
        a, b = law()
        target = torch.rand(2, 4, 8, 8)*.5+.05
        sigma = (a*target+b).sqrt()
        z_hat = anscombe(target, a, b)+1
        loss = mode_loss('anscombe', 'anscombe_l1', dict(corrected=target, z_hat=z_hat), target, sigma, a, b, 1e-5, .01)
        self.assertAlmostEqual(float(loss), .01, places=6)
        blind = mode_loss('blind', 'unweighted L1', dict(corrected=target+.1, z_hat=None), target, sigma, a, b, 1e-5, 1.)
        self.assertAlmostEqual(float(blind), .1, places=6)
        with self.assertRaises(ValueError):
            mode_loss('anscombe', 'unweighted L1', dict(corrected=target, z_hat=z_hat), target, sigma, a, b, 1e-5, 1.)
        with self.assertRaises(ValueError):
            mode_loss('blind', 'anscombe_l1', dict(corrected=target, z_hat=None), target, sigma, a, b, 1e-5, 1.)

    def test_oracle_alpha_measures_the_missing_amplitude(self):
        clean = torch.rand(1, 4, 32, 32)
        noisy = clean+.1*torch.randn(clean.shape, generator=torch.Generator().manual_seed(2))
        self.assertAlmostEqual(oracle_alpha(noisy, noisy-.25*(noisy-clean), clean), 4., places=6)


if __name__ == '__main__':
    unittest.main()
