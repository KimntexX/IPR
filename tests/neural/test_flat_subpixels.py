import unittest
import torch

from audit_flat_subpixels import decompose,head_bias_field
from joint_bayer_unet import colour_tags,JointBayerUNet,sample_rgb
from synthetic_bayer_data import PATTERNS


class FlatSubpixelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def fixture(self):
        pattern=torch.tensor([PATTERNS[0]])
        observed=colour_tags(pattern).reshape(1,3,2,2).repeat(1,1,16,16).bool()
        return pattern,~observed

    def test_perfect_output_has_no_fake_explained_fraction(self):
        p,_=self.fixture();result=decompose(torch.zeros(1,3,32,32),p)
        self.assertEqual(result['total_mse'],0.);self.assertIsNone(result['cell_template_explained_fraction'])

    def test_uniform_missing_colour_bias_is_not_extra_subpixel_bias(self):
        p,missing=self.fixture();error=missing.float()*.1
        result=decompose(error,p)
        self.assertAlmostEqual(result['cell_template_explained_fraction'],1.)
        self.assertEqual(result['extra_periodic_beyond_missing_channel_mean_mse'],0.)
        self.assertGreater(result['periodic_2x2_excluding_dc_mse'],0.)

    def test_energy_identity_with_subpixel_and_nonperiodic_errors(self):
        p,missing=self.fixture();torch.manual_seed(83)
        error=torch.randn(1,3,32,32)*.01*missing
        result=decompose(error,p)
        self.assertGreater(result['within_cell_mse'],0.)
        self.assertAlmostEqual(result['total_mse'],result['dc_mse']+
            result['periodic_2x2_excluding_dc_mse']+result['within_cell_mse'],places=12)

    def test_observed_errors_are_not_silently_explained_as_missing_bias(self):
        p,_=self.fixture()
        with self.assertRaises(ValueError):decompose(torch.ones(1,3,32,32),p)

    def test_output_bias_field_preserves_observed_sites_in_all_phases(self):
        model=JointBayerUNet(4);torch.nn.init.constant_(model.head.bias,.1)
        for phase in PATTERNS:
            p=torch.tensor([phase]);packed=torch.zeros(1,4,16,16)
            for canonical in (False,True):
                field=head_bias_field(model,packed,packed,p,dict(scale=2.,canonical_input=canonical))
                self.assertTrue(torch.equal(sample_rgb(field,p),packed))
                self.assertAlmostEqual(field.max().item(),.2,places=6)


if __name__=='__main__':unittest.main()
