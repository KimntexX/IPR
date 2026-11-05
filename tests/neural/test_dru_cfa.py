import unittest
import torch
from pathlib import Path
from dru_cfa import DruCfaCorrector,support,create


class DruCfaTests(unittest.TestCase):
    @unittest.skipUnless((Path(__file__).resolve().parents[2]/'build-patch_neural_restoration/third-party-drunet-v001/manifest.json').exists(),'Pinned local architecture source required')
    def test_checkpointed_forward_and_gradients_match_official_execution(self):
        torch.set_num_threads(4)
        model,_=create(Path(__file__).resolve().parents[2],9101)
        with torch.no_grad():model.core.m_tail.weight.normal_(0,.001)
        values=torch.randn(1,4,16,16)*.1;noise=torch.full_like(values,.02);tags=torch.tensor([[0,1,3,2]])
        model.eval();expected=model.correct_observations(values,noise,tags)
        expected.square().mean().backward()
        gradients={n:p.grad.clone() for n,p in model.named_parameters()}
        model.zero_grad(set_to_none=True);model.train()
        actual=model.correct_observations(values,noise,tags);actual.square().mean().backward()
        torch.testing.assert_close(actual,expected,rtol=0,atol=0)
        for name,p in model.named_parameters():torch.testing.assert_close(p.grad,gradients[name],rtol=1e-5,atol=1e-8)

    def test_small_photo_halo_is_refused_for_the_larger_architecture(self):
        import numpy as np
        from photo_cfa_tiling import tiled_correct
        model=DruCfaCorrector(torch.nn.Conv2d(8,4,1,bias=False))
        source=np.zeros((32,32),dtype=np.float32)
        with self.assertRaises(ValueError):tiled_correct(model,source,[0,1,3,2],.01,source.copy(),halo=128)

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
