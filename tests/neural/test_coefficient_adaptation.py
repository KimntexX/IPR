import unittest
import torch

from audit_coefficient_adaptation import coefficient_fields


class CoefficientAdaptationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_mean_is_taken_after_tanh_not_before(self):
        logits = torch.zeros(1, 36, 8, 10)
        logits[:, :, :, 5:] = 2.
        bias = torch.ones(36)*.3
        fields, stats = coefficient_fields(logits, bias, border=2)
        torch.testing.assert_close(fields['image_mean'], torch.full_like(logits, float(torch.tanh(torch.tensor(2.)))/2))
        torch.testing.assert_close(fields['bias_only'], torch.full_like(logits, float(torch.tanh(torch.tensor(.3)))))
        self.assertGreater(abs(float(fields['image_mean'][0, 0, 0, 0])-float(logits.mean().tanh())), .1)
        self.assertGreater(stats['mean_channel_spatial_std_core'], 0.)
        self.assertGreater(stats['core_spatial_energy_fraction'], 0.)

    def test_constant_fields_and_zero_energy_are_explicit(self):
        zero = torch.zeros(1, 36, 8, 10)
        fields, stats = coefficient_fields(zero, torch.zeros(36), border=2)
        self.assertIsNone(stats['core_spatial_energy_fraction'])
        self.assertTrue(all(torch.equal(v, zero) for v in fields.values()))
        fields, stats = coefficient_fields(zero+.4, torch.zeros(36)+.4, border=2)
        self.assertLess(stats['mean_channel_spatial_std_core'], 1e-7)
        self.assertLess(stats['core_spatial_energy_fraction'], 1e-10)

    def test_core_mean_excludes_boundaries_without_using_truth(self):
        logits = torch.zeros(1, 36, 8, 10)
        logits[:, :, :2] = 3.
        fields, _ = coefficient_fields(logits, torch.zeros(36), border=2)
        self.assertFalse(fields['core_mean'].any())
        self.assertTrue((fields['image_mean'] > 0).all())
        self.assertEqual(fields['full'].shape, logits.shape)

    def test_missing_nonfinite_and_mismatched_inputs_rejected(self):
        logits = torch.zeros(1, 36, 8, 10)
        bias = torch.zeros(36)
        for field, b in ((None, bias), (logits, None), (logits*float('nan'), bias),
                         (logits, bias[:32]), (logits, bias.double())):
            with self.assertRaises(ValueError):
                coefficient_fields(field, b, border=2)


if __name__ == '__main__':
    unittest.main()
