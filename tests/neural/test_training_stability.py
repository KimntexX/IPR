import unittest
import torch

from audit_training_stability import content_balance,full_train_probe,state_hash,summarize_probe
from camera_unet import CameraUNet


class TrainingStabilityTests(unittest.TestCase):
    def test_state_hash_is_order_independent_but_value_sensitive(self):
        a={'x':torch.ones(3),'y':torch.zeros(2)}
        self.assertEqual(state_hash(a),state_hash(dict(reversed(list(a.items())))))
        self.assertNotEqual(state_hash(a),state_hash(dict(a,x=torch.zeros(3))))

    def test_full_probe_leaves_rng_and_training_state_unchanged(self):
        torch.set_num_threads(2);torch.manual_seed(82)
        model=CameraUNet(4);model.train()
        source=torch.randn(2,3,96,96)*.02
        arrays=dict(signal=source,target=source.clone(),valid=torch.ones(2,1,96,96,dtype=torch.bool),
            sigma=torch.full((2,3),.01))
        records=[dict(scene_group='one'),dict(scene_group='two')]
        before=torch.get_rng_state().clone();weights=state_hash(model.state_dict())
        result=full_train_probe(model,arrays,records,dict(scale=2.))
        self.assertTrue(torch.equal(before,torch.get_rng_state()))
        self.assertEqual(weights,state_hash(model.state_dict()));self.assertTrue(model.training)
        self.assertEqual(result['all']['camera_mse'],0.)

    def test_objective_units_and_bias_are_not_averaged_across_patches_first(self):
        row=dict(pixels=10,sse=30.,mean_square_bias=4.,bias_sum=[10.,20.,30.])
        summary=summarize_probe([row],2.,1.)
        self.assertEqual(summary['normalized_objective'],1.25)
        self.assertEqual(summary['camera_channel_bias'],[1.,2.,3.])
        with self.assertRaises(ValueError):summarize_probe([],2.,0.)

    def test_balance_exposes_objective_imbalance_and_unique_raws(self):
        records=[dict(id='a',scene_group='one',source_sha256='same'),dict(id='b',scene_group='two',source_sha256='other')]
        evidence=[dict(id=name,full=dict(input_sse=sse),structure={b:dict(components=0) for b in ('low','weak','strong')})
            for name,sse in [('a',99.),('b',1.)]]
        result=content_balance(records,evidence)
        self.assertEqual(result['one']['patch_sampling_fraction'],.5)
        self.assertEqual(result['one']['input_sse_fraction'],.99)
        with self.assertRaises(ValueError):content_balance(records,evidence[:1])


if __name__=='__main__':unittest.main()
