import unittest

import torch

from joint_bayer_unet import JointBayerUNet,bilinear,sample_rgb
from synthetic_bayer_data import PATTERNS,FAMILIES,signal,noisy_observation


class JointBayerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def test_constants_signed_and_overwhite_all_phases(self):
        rgb=torch.tensor([-.1,.3,1.2])[None,:,None,None].expand(1,3,32,32).clone()
        for values in PATTERNS:
            p=torch.tensor([values]);packed=sample_rgb(rgb,p)
            torch.testing.assert_close(bilinear(packed,p),rgb,rtol=1e-6,atol=1e-7)

    def test_affine_rgb_reconstructed_interior_all_phases(self):
        y,x=torch.meshgrid(torch.arange(32)/32,torch.arange(32)/32,indexing='ij')
        rgb=torch.stack((x,y,x+y))[None]
        for values in PATTERNS:
            p=torch.tensor([values]);out=bilinear(sample_rgb(rgb,p),p)
            torch.testing.assert_close(out[:,:,2:-2,2:-2],rgb[:,:,2:-2,2:-2],rtol=1e-6,atol=1e-7)

    def test_network_initially_baseline_and_input_unchanged(self):
        torch.manual_seed(72);model=JointBayerUNet(4)
        rgb=torch.rand(1,3,32,32);p=torch.tensor([PATTERNS[3]])
        packed=sample_rgb(rgb,p);before=packed.clone();sigma=torch.ones_like(packed)*.01
        self.assertTrue(torch.equal(model(packed,sigma,p),bilinear(packed,p)))
        self.assertTrue(torch.equal(before,packed))
        model(packed,sigma,p).square().mean().backward()
        self.assertTrue(all(v.grad is None or torch.isfinite(v.grad).all() for v in model.parameters()))

    def test_unobserved_values_are_not_identifiable(self):
        p=torch.tensor([PATTERNS[0]]);a=torch.zeros(1,3,32,32);b=a.clone()
        b[:,0,1::2,1::2]=.5  # Red values where the sensor measures blue.
        self.assertFalse(torch.equal(a,b))
        self.assertTrue(torch.equal(sample_rgb(a,p),sample_rgb(b,p)))

    def test_invalid_pattern_and_sigma_rejected(self):
        model=JointBayerUNet(4);packed=torch.zeros(1,4,16,16);p=torch.tensor([PATTERNS[0]])
        with self.assertRaises(ValueError):model(packed,-torch.ones_like(packed),p)
        with self.assertRaises(ValueError):model(packed,packed,torch.tensor([[0,0,1,2]]))
        bad=packed.clone();bad[0,0,0,0]=float('nan')
        with self.assertRaises(ValueError):model(bad,packed,p)

    def test_generator_does_not_change_global_rng_and_noise_zero_is_identity(self):
        before=torch.get_rng_state().clone()
        for family in FAMILIES:
            rgb=signal(family,71,32)[None]
            self.assertTrue(torch.isfinite(rgb).all())
            p=torch.tensor([PATTERNS[0]])
            noisy,sigma=noisy_observation(rgb,p,0.,0.,torch.Generator().manual_seed(19))
            self.assertTrue(torch.equal(noisy,sample_rgb(rgb,p)));self.assertEqual(sigma.max().item(),0.)
        self.assertTrue(torch.equal(before,torch.get_rng_state()))


if __name__=='__main__':unittest.main()
