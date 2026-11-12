import unittest
import torch

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation
from joint_bayer_unet import sample_rgb
from moment_bayer_unet import MomentBayerUNet
from run_symmetrized_moment import both_predictions, average_views
from synthetic_bayer_data import PATTERNS


class SymmetrizedMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def model(self):
        torch.manual_seed(101)
        model = MomentBayerUNet(4)
        torch.nn.init.uniform_(model.head.weight, -.08, .08)
        torch.nn.init.uniform_(model.head.bias, -.08, .08)
        model.requires_grad_(False)
        return model.eval()

    def test_non_square_signed_output_commutes_with_all_d4_operations(self):
        model = self.model()
        target = torch.rand(1, 3, 32, 40)*1.7-.3
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, pattern)
        before = packed.clone()
        with torch.inference_mode():
            _, expected = both_predictions(model, packed, pattern, 2.)
            for operation in OPERATIONS:
                p, tags = transform_observation(packed, pattern, operation)
                actual = transform(both_predictions(model, p, tags, 2.)[1], operation, inverse=True)
                torch.testing.assert_close(actual, expected, rtol=1e-6, atol=5e-7)
                self.assertTrue(torch.equal(sample_rgb(actual, pattern), packed))
        self.assertTrue(torch.equal(packed, before))

    def test_constant_and_interior_affine_reproduction(self):
        model = self.model()
        y, x = torch.meshgrid(torch.arange(32)/32, torch.arange(40)/40, indexing='ij')
        fields = (torch.tensor([-.2, .4, 1.2])[None, :, None, None].expand(1, 3, 32, 40),
            torch.stack((-.1+x*.3, .2-y*.1, 1.2+x*.1-y*.2))[None])
        with torch.inference_mode():
            for target in fields:
                for p in PATTERNS:
                    pattern = torch.tensor([p])
                    packed = sample_rgb(target, pattern)
                    image = both_predictions(model, packed, pattern, 2.)[1]
                    torch.testing.assert_close(image[:, :, 4:-4, 4:-4], target[:, :, 4:-4, 4:-4], rtol=1e-6, atol=5e-7)

    def test_sensor_projection_retains_subnormal_values_after_averaging(self):
        target = torch.zeros(1, 3, 32, 40)
        target[:, 0] = torch.nextafter(torch.tensor(0.), torch.tensor(1.))
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, pattern)
        output = average_views(target*.5, target*.5, packed, pattern)
        self.assertTrue(torch.equal(sample_rgb(output, pattern), packed))

    def test_mismatched_or_nonfinite_views_rejected(self):
        first = torch.zeros(1, 3, 32, 40)
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(first, pattern)
        for bad in (first[:, :, :, :32], first*float('nan'), first*float('inf')):
            with self.assertRaises(ValueError):
                average_views(first, bad, packed, pattern)


if __name__ == '__main__':
    unittest.main()
