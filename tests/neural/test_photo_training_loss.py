import unittest
import torch

from photo_training_loss import channel_bias_loss,objective
from camera_data import masked_mse


class PhotoTrainingLossTests(unittest.TestCase):
    def fixture(self):
        target=torch.zeros((2,3,96,96));mask=torch.ones((2,1,96,96),dtype=torch.bool)
        return target,mask

    def test_constant_bias_is_penalized_per_patch_without_cancellation(self):
        target,mask=self.fixture();prediction=target.clone();prediction[0]=.1;prediction[1]=-.1
        self.assertAlmostEqual(channel_bias_loss(prediction,target,mask).item(),.01,places=6)

    def test_zero_weight_keeps_original_objective_exact(self):
        target,mask=self.fixture();prediction=target+.2
        total,pixel,bias=objective(prediction,target,mask)
        self.assertTrue(torch.equal(total,masked_mse(prediction,target,mask)))
        self.assertTrue(torch.equal(total,pixel));self.assertEqual(bias.item(),0.)

    def test_unit_weight_doubles_constant_error_without_bias_clamp(self):
        target,mask=self.fixture();prediction=(target-.1).requires_grad_()
        loss,pixel,bias=objective(prediction,target,mask,1.)
        self.assertAlmostEqual(loss.item(),.02,places=6)
        self.assertAlmostEqual(pixel.item(),bias.item(),places=6)
        loss.backward();self.assertTrue(torch.isfinite(prediction.grad).all())

    def test_border_excluded_and_invalid_mask_refused(self):
        target,mask=self.fixture();prediction=target.clone();prediction[:,:,:24]=10
        self.assertEqual(channel_bias_loss(prediction,target,mask).item(),0.)
        mask[0]=False
        with self.assertRaises(ValueError):channel_bias_loss(prediction,target,mask)

    def test_invalid_weights_and_nonfinite_values_refused(self):
        target,mask=self.fixture()
        for w in (-1,float('nan'),True):
            with self.assertRaises(ValueError):objective(target,target,mask,w)
        bad=target.clone();bad[0,0,0,0]=float('inf')
        with self.assertRaises(ValueError):channel_bias_loss(bad,target,mask)


if __name__=='__main__':unittest.main()
