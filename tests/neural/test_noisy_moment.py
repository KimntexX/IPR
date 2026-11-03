import unittest

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from bayer_canonical import predict
from joint_bayer_unet import sample_rgb
from local_moment_unet import LocalMomentUNet
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction
from prepare_closed_inference import prepare_inference
from synthetic_bayer_data import PATTERNS


class NoisyMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def setUp(self):
        torch.manual_seed(931)
        self.clean = LocalMomentUNet(4).eval()
        for h in (self.clean.head, self.clean.local_head):
            torch.nn.init.uniform_(h.weight, -.1, .1)
        self.model = NoisyMomentUNet(4).eval()
        load_reconstruction(self.model, self.clean.state_dict())

    def test_zero_noise_is_exact_for_all_phases_even_with_trained_correction(self):
        torch.nn.init.uniform_(self.model.noise_head.weight, -.4, .4)
        torch.nn.init.uniform_(self.model.noise_head.bias, -.4, .4)
        truth = torch.rand(1, 3, 32, 40)*1.7-.3
        with torch.inference_mode():
            for values in PATTERNS:
                tags = torch.tensor([values])
                packed = sample_rgb(truth, tags)
                args = (packed, torch.zeros_like(packed), tags)
                expected = predict(self.clean, *args, True, True)
                self.assertTrue(torch.equal(predict(self.model, *args, False, True), expected))
                self.assertTrue(torch.equal(sample_rgb(expected, tags), packed))

    def test_correction_bound_zero_sigma_sites_signed_values_and_gradient(self):
        tags = torch.tensor([PATTERNS[0]])
        packed = torch.full((1, 4, 16, 20), -.2)
        sigma = torch.full_like(packed, .03)
        sigma[:, 0] = 0
        with torch.no_grad():
            self.model.noise_head.bias.fill_(1.)
        corrected = self.model.correct_observations(packed, sigma, tags)
        self.assertTrue(torch.equal(corrected[:, 0], packed[:, 0]))
        self.assertTrue(((corrected-packed).abs() <= 3*sigma+1e-7).all())
        self.assertTrue((corrected < 0).all())
        output = self.model(packed, sigma, tags)
        output.square().mean().backward()
        self.assertTrue(torch.isfinite(self.model.noise_head.weight.grad).all())
        self.assertGreater(float(self.model.noise_head.weight.grad.abs().sum()), 0)
        self.assertFalse(torch.equal(sample_rgb(output.detach(), tags), packed))
        with self.assertRaises(ValueError):
            self.model(packed, sigma, tags, True)
        with self.assertRaises(ValueError):
            self.model(packed, -sigma, tags)
        with self.assertRaises(ValueError):
            self.model(packed*float('nan'), sigma, tags)
        with self.assertRaises(ValueError):
            load_reconstruction(self.model, {})

    def test_prepared_bit_identity_and_noisy_d4_covariance(self):
        torch.nn.init.uniform_(self.model.noise_head.weight, -.5, .5)
        torch.nn.init.uniform_(self.model.noise_head.bias, -.5, .5)
        prepared = prepare_inference(self.model)
        truth = torch.rand(1, 3, 128, 144)*1.5-.2
        with torch.inference_mode():
            for values in PATTERNS:
                tags = torch.tensor([values])
                packed = sample_rgb(truth, tags)
                sigma = torch.rand_like(packed)*.03
                image = predict(self.model, packed, sigma, tags, False, True)
                self.assertTrue(torch.equal(image, predict(prepared, packed, sigma, tags, False, True)))
                for op in OPERATIONS:
                    tp, labels = transform_observation(packed, tags, op)
                    ts, _ = transform_observation(sigma, tags, op)
                    result = transform(predict(self.model, tp, ts, labels, False, True), op, True)
                    self.assertLess(float((image-result)[:, :, 48:-48, 48:-48].abs().max()), 2e-6)


if __name__ == '__main__':
    unittest.main()
