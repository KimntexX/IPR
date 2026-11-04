import unittest

import torch

from bayer_canonical import predict
from multiscale_correction import composed_support
from noisy_moment_unet import NoisyMomentUNet
from synthetic_bayer_data import PATTERNS


class MultiscaleCorrectionTests(unittest.TestCase):
    def test_support_and_aligned_window_agreement_with_active_context(self):
        torch.set_num_threads(2)
        bound = composed_support()
        self.assertEqual(bound['conservative_full_pixel_halo'], 104)
        self.assertEqual(bound['training_side']-2*bound['scoring_border'], 32)
        torch.manual_seed(711)
        model = NoisyMomentUNet(4).eval()
        for head in (model.noise_head, model.head, model.local_head, model.noise_features.statistics):
            torch.nn.init.uniform_(head.weight, -.04, .04)
        packed = torch.randn(1, 4, 192, 192)*.02+.3
        sigma = torch.full_like(packed, .02)
        tags = torch.tensor([PATTERNS[0]])
        with torch.inference_mode():
            full = predict(model, packed, sigma, tags, False, True)
            part = predict(model, packed[:, :, 24:-24, 24:-24], sigma[:, :, 24:-24, 24:-24], tags, False, True)
        self.assertLess(float((full[:, :, 160:224, 160:224]-part[:, :, 112:176, 112:176]).abs().max()), 2e-6)
