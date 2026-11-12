import unittest
import torch
from joint_bayer_unet import sample_rgb
from sensor_curve_noise import observation


class SensorCurveNoiseTests(unittest.TestCase):
    def test_invalid_batch_and_gain_are_rejected(self):
        with self.assertRaises(ValueError):observation(torch.ones(2,3,32,32),torch.tensor([[0,1,3,2],[2,3,1,0]]),None,1)
        recipe=dict(parameters=[dict(colour=c,a=1.,b=1.,gain=-1.,divisor=100.) for c in range(4)])
        with self.assertRaises(ValueError):observation(torch.ones(1,3,32,32),torch.tensor([[0,1,3,2]]),recipe,1)

    def test_zero_noise_and_physical_colour_variances(self):
        truth=torch.full((1,3,32,32),.2);tags=torch.tensor([[2,3,1,0]])
        clean,zero=observation(truth,tags,None,81)
        self.assertTrue(torch.equal(clean,sample_rgb(truth,tags)));self.assertEqual(float(zero.sum()),0)
        scenario=dict(parameters=[dict(colour=c,a=0.,b=float(c+1),gain=2.,divisor=100.) for c in range(4)])
        noisy,sigma=observation(truth,tags,scenario,81)
        for i,colour in enumerate(tags[0].tolist()):
            torch.testing.assert_close(sigma[:,i],torch.full_like(sigma[:,i],(colour+1)**.5*.02))
        again,_=observation(truth,tags,scenario,81);self.assertTrue(torch.equal(noisy,again))
        self.assertFalse(torch.equal(noisy,clean))


if __name__=='__main__':unittest.main()
