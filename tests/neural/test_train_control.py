import unittest
import torch

from evaluate_context_ablation import score
from evaluate_train_control import input_bias_sums,summarize_with_bias,grouped_summaries


class TrainControlTests(unittest.TestCase):
    def fixture(self):
        target=torch.zeros((1,3,96,96),dtype=torch.float32)
        source=target+.1;output=target+.04;mask=torch.ones((1,1,96,96),dtype=torch.bool)
        row=score(dict(source_sha256='s',scene_group='train',iso=100,split='train'),source,target,output,mask,[0.,.01])
        row['full']['input_channel_bias_sum']=input_bias_sums(source,target,mask)
        core=mask.clone();core[:,:,:24]=False;core[:,:,-24:]=False;core[:,:,:,:24]=False;core[:,:,:,-24:]=False
        row['core24']['input_channel_bias_sum']=input_bias_sums(source,target,core)
        return row

    def test_bias_distinguishes_reference_offset_and_network_shift(self):
        metrics=summarize_with_bias([self.fixture()])
        for region in ('full','core24'):
            torch.testing.assert_close(torch.tensor(metrics[region]['input_camera_channel_bias']),torch.full((3,),.1))
            torch.testing.assert_close(torch.tensor(metrics[region]['output_minus_input_camera_bias']),torch.full((3,),-.06))

    def test_groups_do_not_count_patches_as_independent_sources(self):
        row=self.fixture();groups=grouped_summaries([row,row],'scene_group')
        self.assertEqual(groups['train']['patches'],2)
        self.assertEqual(groups['train']['unique_source_raws'],1)

    def test_invalid_bias_inputs_rejected(self):
        source=torch.zeros((1,3,8,8));valid=torch.ones((1,1,8,8),dtype=torch.bool)
        with self.assertRaises(ValueError):input_bias_sums(source,source[:,:,:4],valid)
        source[0,0,0,0]=float('nan')
        with self.assertRaises(ValueError):input_bias_sums(source,source,valid)

    def test_empty_groups_are_not_invented(self):
        self.assertEqual(grouped_summaries([],'split'),{})


if __name__=='__main__':unittest.main()
