import unittest
import torch

from joint_bayer_unet import sample_rgb
from noisy_cfa_contract import prepare_synthetic, joint_loss
from synthetic_bayer_data import PATTERNS


class NoisyCfaContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_observation_conditioning_scale_and_signed_unclipped_domain(self):
        target = torch.tensor([-.2, .4, 1.3])[None, :, None, None].expand(1, 3, 32, 40).clone()
        before = target.clone()
        p = torch.tensor([PATTERNS[0]])
        example = prepare_synthetic(target, p, .0008, .0004, 113)
        expected_sigma = (.0008*(example.observation*2).clamp_min(0)+.0004).sqrt()/2
        torch.testing.assert_close(example.sigma, expected_sigma)
        self.assertTrue((example.observation < 0).any())
        self.assertTrue((example.observation > .5).any())
        self.assertTrue(torch.equal(target, before))
        self.assertTrue(torch.equal(example.target_rgb, target/2))

    def test_zero_noise_and_reproducibility_without_global_rng_change(self):
        target = torch.rand(1, 3, 32, 40)
        p = torch.tensor([PATTERNS[0]])
        state = torch.get_rng_state().clone()
        first = prepare_synthetic(target, p, 0., .0004, 114)
        second = prepare_synthetic(target, p, 0., .0004, 114)
        self.assertTrue(torch.equal(state, torch.get_rng_state()))
        self.assertTrue(torch.equal(first.observation, second.observation))
        zero = prepare_synthetic(target, p, 0., 0., 114)
        self.assertTrue(torch.equal(zero.observation, sample_rgb(target/2, p)))
        self.assertFalse(zero.sigma.any())

    def test_clean_observed_values_can_be_supervised_and_noise_is_not_copied(self):
        target = torch.ones(1, 3, 32, 40)*.4
        p = torch.tensor([PATTERNS[0]])
        example = prepare_synthetic(target, p, 0., .0004, 115)
        valid = torch.ones(1, 1, 32, 40, dtype=torch.bool)
        self.assertFalse(torch.equal(sample_rgb(example.target_rgb, p), example.observation))
        self.assertEqual(float(joint_loss(example.target_rgb, example, valid)), 0.)
        with self.assertRaises(ValueError):
            joint_loss(example.target_rgb*float('nan'), example, valid)

    def test_invalid_coefficients_and_normalization_rejected(self):
        target = torch.ones(1, 3, 32, 40)
        p = torch.tensor([PATTERNS[0]])
        for a, b, scale in ((float('nan'), 0., 2.), (-1., 0., 2.), (0., 0., 0.), (0., 0., float('inf'))):
            with self.assertRaises(ValueError):
                prepare_synthetic(target, p, a, b, 1, scale)
        with self.assertRaises(ValueError):
            prepare_synthetic(None, p, 0., 0., 1)
        with self.assertRaises(ValueError):
            prepare_synthetic(target, None, 0., 0., 1)
        with self.assertRaises(ValueError):
            joint_loss(target, None, torch.ones(1, 1, 32, 40, dtype=torch.bool))


if __name__ == '__main__':
    unittest.main()
