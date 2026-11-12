import unittest
import torch
from torch.nn import functional as F

from audit_bayer_symmetries import OPERATIONS, transform, transform_observation, fixed_prediction, transpose_supports, difference
from joint_bayer_unet import sample_rgb, bilinear
from synthetic_bayer_data import PATTERNS


class BayerSymmetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def fixture(self):
        generator = torch.Generator().manual_seed(97)
        return torch.rand(1, 3, 32, 40, generator=generator)*1.7-.3

    def test_non_square_signed_cfa_and_tag_roundtrip_all_phases_and_operations(self):
        truth = self.fixture()
        variants = list(PATTERNS)+[tuple(3 if c == 1 else 1 if c == 3 else c for c in p) for p in PATTERNS]
        for values in variants:
            pattern = torch.tensor([values])
            packed = sample_rgb(truth, pattern)
            for operation in OPERATIONS:
                p, tags = transform_observation(packed, pattern, operation)
                self.assertTrue(torch.equal(p, sample_rgb(transform(truth, operation), tags)))
                restored = F.pixel_unshuffle(transform(F.pixel_shuffle(p, 2), operation, inverse=True), 2)
                labels = transform(tags.reshape(1, 1, 2, 2), operation, inverse=True).reshape(1, 4)
                self.assertTrue(torch.equal(restored, packed))
                self.assertTrue(torch.equal(labels, pattern))

    def test_bilinear_equivariance_and_fixed_witness_sensor_copy(self):
        truth = self.fixture()
        pattern = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(truth, pattern)
        for operation in OPERATIONS:
            p, tags = transform_observation(packed, pattern, operation)
            base = transform(bilinear(p, tags), operation, inverse=True)
            torch.testing.assert_close(base, bilinear(packed, pattern), rtol=1e-6, atol=5e-7)
            for kind in ('zero_weights', 'symmetric_candidates', 'all_candidates'):
                image = transform(fixed_prediction(p, tags, kind), operation, inverse=True)
                self.assertTrue(torch.equal(sample_rgb(image, pattern), packed))
        torch.testing.assert_close(fixed_prediction(packed, pattern, 'zero_weights'), bilinear(packed, pattern))

    def test_support_list_distinguishes_symmetric_from_one_sided_candidates(self):
        rows = transpose_supports()
        self.assertEqual(len(rows), 32)
        self.assertTrue(all(r['present'] for r in rows if r['candidate'] < 2))
        self.assertTrue(any(not r['present'] for r in rows if r['candidate'] >= 2))

    def test_difference_support_invalid_inputs_and_known_delta(self):
        image = self.fixture()
        result = difference(image, image+.1, border=4)
        self.assertEqual(result['components'], 3*24*32)
        self.assertAlmostEqual(result['mean_squared_difference'], .01, places=6)
        with self.assertRaises(ValueError):
            difference(image, image*float('nan'), border=4)
        with self.assertRaises(ValueError):
            transform(image, (4, False))
        with self.assertRaises(ValueError):
            fixed_prediction(sample_rgb(image, torch.tensor([PATTERNS[0]])), torch.tensor([PATTERNS[0]]), 'unknown')


if __name__ == '__main__':
    unittest.main()
