"""Fixed transfer metric contracts: identity, colour bias and structure loss."""
import unittest
from pathlib import Path
import tempfile
import torch

from neural_transfer_metrics import (structure_thresholds,core_mask,error_sums,
                                     gradient_sums,summarize)
from evaluate_reserved_transfer import load_frozen_model
from camera_unet import CameraUNet
from diagnose_train_learnability import digest


class TransferMetricsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def fixture(self):
        y,x=torch.meshgrid(torch.arange(96),torch.arange(96),indexing='ij')
        target=(.01*x+.003*y)[None,None].float().expand(1,3,96,96).clone()
        return target,torch.ones((1,1,96,96),dtype=torch.bool)

    def row(self,source,target,output,mask):
        return dict(full=error_sums(source,target,output,mask),
                    core24=error_sums(source,target,output,core_mask(mask)),
                    structure=gradient_sums(source,target,output,mask,[0.,.001]))

    def test_identity_is_exact_and_zero_baseline_is_not_a_fake_gain(self):
        target,mask=self.fixture();summary=summarize([self.row(target,target,target,mask)])
        self.assertEqual(summary['full']['input_mse'],0.)
        self.assertEqual(summary['full']['output_mse'],0.)
        self.assertIsNone(summary['full']['mse_ratio'])
        self.assertAlmostEqual(summary['structure']['strong']['output_gradient_gain'],1.,places=6)

    def test_flattening_cannot_pass_as_preserved_structure(self):
        target,mask=self.fixture();output=target.mean()*torch.ones_like(target)
        summary=summarize([self.row(target,target,output,mask)])
        self.assertEqual(summary['structure']['strong']['output_gradient_gain'],0.)
        self.assertGreater(summary['structure']['strong']['output_gradient_sse'],0.)

    def test_camera_colour_bias_is_measured_including_signed_values(self):
        target,mask=self.fixture();target-=.3
        bias=torch.tensor([.1,0.,-.1])[None,:,None,None]
        summary=summarize([self.row(target,target,target+bias,mask)])
        for actual,expected in zip(summary['core24']['camera_channel_bias'],[.1,0.,-.1]):
            self.assertAlmostEqual(actual,expected,places=6)
        self.assertGreater(summary['core24']['output_mse'],0.)
        self.assertIsNone(summary['core24']['opponent_mse_ratio'])

    def test_context_holes_and_patch_borders_are_not_scored_as_structure(self):
        target,mask=self.fixture();mask[:,:,48,48]=False
        stats=gradient_sums(target,target,target,mask,[0.,.001])
        self.assertEqual(sum(r['components'] for r in stats.values()),2*(48*48-25))
        self.assertEqual(int(core_mask(torch.ones_like(mask)).sum()),48*48)

    def test_aggregation_weights_pixels_not_per_patch_ratios(self):
        target,mask=self.fixture();source=target+.1
        first=self.row(source,target,target+.05,mask)
        half=mask.clone();half[:,:,:48]=False
        second=self.row(source,target,target+.1,half)
        summary=summarize([first,second])
        self.assertAlmostEqual(summary['full']['mse_ratio'],.5,places=5)

    def test_thresholds_are_finite_ordered_and_flat_bands_are_disjoint(self):
        target,mask=self.fixture();low,high=structure_thresholds(target,mask)
        self.assertTrue(0<=low<=high)
        flat=torch.zeros_like(target)
        stats=gradient_sums(flat,flat,flat,mask,[0.,0.])
        self.assertGreater(stats['low']['components'],0)
        self.assertEqual(stats['weak']['components'],0);self.assertEqual(stats['strong']['components'],0)
        with self.assertRaises(ValueError):core_mask(mask,48)
        with self.assertRaises(ValueError):structure_thresholds(target,torch.zeros_like(mask))

    def test_empty_region_is_reported_not_invented_as_zero_error(self):
        target,mask=self.fixture();empty=torch.zeros_like(mask)
        summary=summarize([self.row(target,target,target,empty)])
        self.assertEqual(summary['full'],dict(pixels=0,status='no_valid_support'))
        mixed=summarize([self.row(target,target,target,mask),self.row(target,target,target,empty)])
        self.assertEqual(mixed['full']['pixels'],96*96)

    def test_only_exact_frozen_checkpoint_is_loaded_without_trainable_parameters(self):
        with tempfile.TemporaryDirectory() as name:
            folder=Path(name);weights=folder/'real_training_long-diagnostic-weights.pt'
            torch.save(dict(state_dict=CameraUNet(4).state_dict(),width=4,diagnostic='real_training_long'),weights)
            proof=dict(status='completed_training_only_diagnostics',freeze=dict(config=dict(width=4)),
                cases=[dict(name='real_training_long',steps=2048,weights_sha256=digest(weights))])
            model,_=load_frozen_model(folder,proof)
            self.assertFalse(model.training);self.assertTrue(all(not p.requires_grad for p in model.parameters()))
            proof['cases'][0]['steps']=1024
            with self.assertRaisesRegex(ValueError,'2048'):load_frozen_model(folder,proof)
            proof['cases'][0]['steps']=2048;proof['cases'][0]['weights_sha256']='changed'
            with self.assertRaisesRegex(ValueError,'weights'):load_frozen_model(folder,proof)


if __name__=='__main__':unittest.main()
