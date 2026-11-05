import unittest
import torch
from dru_cfa import DruCfaCorrector,support


class DruCfaTests(unittest.TestCase):
    def test_support_and_context_cover_all_stride_phases(self):
        bound=support()
        self.assertEqual(bound['packed_radius'],185);self.assertEqual(bound['packed_halo'],192)
        self.assertEqual(bound['packed_context'],512);self.assertEqual(bound['cfa_halo'],384)
        for r in bound['intervals']:
            self.assertGreaterEqual(r['low'],r['output']-192);self.assertLessEqual(r['high'],r['output']+192)

    def test_residual_identity_signed_values_and_zero_sigma_sites(self):
        core=torch.nn.Conv2d(8,4,1,bias=False);torch.nn.init.zeros_(core.weight)
        model=DruCfaCorrector(core)
        values=torch.linspace(-.3,1.4,1024).reshape(1,4,16,16);original=values.clone()
        sigma=torch.full_like(values,.02);tags=torch.tensor([[0,1,3,2]])
        self.assertTrue(torch.equal(model.correct_observations(values,sigma,tags),values))
        with torch.no_grad():core.weight.fill_(.01)
        sigma[:,:,:8]=0
        corrected=model.correct_observations(values,sigma,tags)
        self.assertTrue(torch.equal(corrected[:,:,:8],values[:,:,:8]))
        self.assertTrue(torch.equal(values,original))
        corrected.square().mean().backward();self.assertTrue(torch.isfinite(core.weight.grad).all())
        with self.assertRaises(ValueError):model.correct_observations(values,sigma,torch.tensor([[2,3,1,0]]))


if __name__=='__main__':unittest.main()
