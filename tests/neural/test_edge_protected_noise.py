import unittest

import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from bayer_canonical import predict
from edge_protected_noise import EdgeProtectedNoisyUNet, edge_confidence
from joint_bayer_unet import sample_rgb
from local_moment_unet import LocalMomentUNet
from noisy_moment_unet import load_reconstruction
from prepare_closed_inference import prepare_inference
from synthetic_bayer_data import PATTERNS


class EdgeProtectionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_diagonal_quarter_rate_structure_is_protected(self):
        y, x = torch.meshgrid(torch.arange(32), torch.arange(40), indexing='ij')
        for coordinate in (x+y, x-y):
            carrier = torch.tensor([1., 0., -1., 0.])[coordinate.remainder(4)]
            packed = (.3+.1*carrier)[None, None].expand(1, 4, -1, -1).contiguous()
            sigma = torch.full_like(packed, .005)
            # The old centre-excluding H/V contrasts vanish on this exact
            # lattice signal, despite strong diagonal curvature.
            confidence = edge_confidence(packed, sigma)
            self.assertLess(float(confidence[:, :, 4:-4, 4:-4].max()), 1e-10)

    def test_partial_diagonal_confidence_is_covariant(self):
        y, x = torch.meshgrid(torch.arange(32), torch.arange(40), indexing='ij')
        carrier = torch.tensor([1., 0., -1., 0.])[(x+y).remainder(4)]
        packed = (.3+.014*carrier)[None, None].expand(1, 4, -1, -1).contiguous()
        sigma = torch.full_like(packed, .005)
        tags = torch.tensor([PATTERNS[0]])
        expected = edge_confidence(packed, sigma)
        self.assertTrue(((expected[:, :, 4:-4, 4:-4] > 0)&(expected[:, :, 4:-4, 4:-4] < 1)).all())
        for op in OPERATIONS:
            values, _ = transform_observation(packed, tags, op)
            deviations, _ = transform_observation(sigma, tags, op)
            actual = transform(edge_confidence(values, deviations), op, True)
            self.assertLess(float((actual-expected)[:, :, 4:-4, 4:-4].abs().max()), 2e-6)

    def test_step_protected_flat_active_and_isolated_four_sigma_not_classified_as_edge(self):
        packed = torch.full((1, 4, 32, 40), -.1)
        sigma = torch.full_like(packed, .02)
        self.assertTrue((edge_confidence(packed, sigma) == 1).all())
        step = packed.clone()
        step[:, :, :, 20:] += .5
        confidence = edge_confidence(step, sigma)
        self.assertLess(float(confidence[:, :, :, 18:22].max()), 1e-10)
        self.assertTrue((confidence[:, :, :, :12] == 1).all())
        spike = packed.clone()
        spike[:, :, 16, 20] += .08
        self.assertGreater(float(edge_confidence(spike, sigma)[0, 0, 16, 20]), .99)
        self.assertTrue(torch.equal(confidence, edge_confidence(step/2, sigma/2)))
        y, x = torch.meshgrid(torch.linspace(-1, 1, 32), torch.linspace(-1, 1, 40), indexing='ij')
        ramp = (x*.3-y*.2)[None, None].expand(1, 4, -1, -1).contiguous()
        self.assertTrue((edge_confidence(ramp, sigma)[:, :, 4:-4, 4:-4] == 1).all())

    def test_zero_noise_parent_identity_and_prepared_noisy_covariance(self):
        torch.manual_seed(901)
        witness = LocalMomentUNet(4).eval()
        model = EdgeProtectedNoisyUNet(4).eval()
        load_reconstruction(model, witness.state_dict())
        torch.nn.init.uniform_(model.noise_head.weight, -.2, .2)
        torch.nn.init.uniform_(model.noise_head.bias, -.2, .2)
        prepared = prepare_inference(model)
        truth = torch.rand(1, 3, 128, 144)*1.5-.2
        with torch.inference_mode():
            for p in PATTERNS:
                tags = torch.tensor([p])
                packed = sample_rgb(truth, tags)
                zero = torch.zeros_like(packed)
                self.assertTrue(torch.equal(predict(model, packed, zero, tags, False, True),
                    predict(witness, packed, zero, tags, True, True)))
                sigma = torch.full_like(packed, .02)
                image = predict(model, packed, sigma, tags, False, True)
                self.assertTrue(torch.equal(image, predict(prepared, packed, sigma, tags, False, True)))
                for op in OPERATIONS:
                    tp, labels = transform_observation(packed, tags, op)
                    ts, _ = transform_observation(sigma, tags, op)
                    other = transform(predict(model, tp, ts, labels, False, True), op, True)
                    self.assertLess(float((image-other)[:, :, 48:-48, 48:-48].abs().max()), 2e-6)


if __name__ == '__main__':
    unittest.main()
