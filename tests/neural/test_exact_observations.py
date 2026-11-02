import unittest
import torch

from joint_bayer_unet import JointBayerUNet,sample_rgb,sparse_rgb,project_exact_observations
from synthetic_bayer_data import PATTERNS


class ExactObservationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def test_projection_is_exact_all_phases_and_missing_values_unchanged(self):
        rgb=torch.linspace(-.2,1.3,3*32*32).reshape(1,3,32,32)
        for pattern in PATTERNS:
            p=torch.tensor([pattern]);packed=sample_rgb(rgb,p)
            predicted=torch.full_like(rgb,.75);before=predicted.clone()
            projected=project_exact_observations(predicted,packed,torch.zeros_like(packed),p)
            self.assertTrue(torch.equal(sample_rgb(projected,p),packed))
            _,mask=sparse_rgb(packed,p)
            self.assertTrue(torch.equal(projected[mask==0],predicted[mask==0]))
            self.assertTrue(torch.equal(predicted,before))

    def test_noisy_projection_is_rejected(self):
        p=torch.tensor([PATTERNS[0]]);packed=torch.zeros(1,4,16,16)
        with self.assertRaises(ValueError):project_exact_observations(torch.zeros(1,3,32,32),packed,torch.ones_like(packed)*.01,p)

    def test_gradients_flow_only_to_unobserved_predictions(self):
        p=torch.tensor([PATTERNS[0]]);packed=torch.ones(1,4,16,16)*-.1
        predicted=torch.ones(1,3,32,32,requires_grad=True)
        projected=project_exact_observations(predicted,packed,torch.zeros_like(packed),p)
        projected.square().sum().backward();_,mask=sparse_rgb(packed,p)
        self.assertEqual(predicted.grad[mask>0].abs().sum().item(),0.)
        self.assertGreater(predicted.grad[mask==0].abs().sum().item(),0.)

    def test_model_policy_does_not_freeze_observations_in_noisy_regime(self):
        model=JointBayerUNet(4);torch.nn.init.constant_(model.head.bias,.1)
        p=torch.tensor([PATTERNS[0]]);packed=torch.full((1,4,16,16),-.1);zero=torch.zeros_like(packed)
        exact=model(packed,zero,p,exact_observations=True)
        self.assertTrue(torch.equal(sample_rgb(exact,p),packed))
        noisy=model(packed,zero+.01,p)
        self.assertFalse(torch.equal(sample_rgb(noisy,p),packed))


if __name__=='__main__':unittest.main()
