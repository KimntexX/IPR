import unittest
import torch

from bayer_canonical import predict
from joint_bayer_unet import bilinear,sample_rgb
from moment_bayer_unet import MomentBayerUNet,interpolate_weights,STENCILS,model_from_config
from synthetic_bayer_data import PATTERNS


class MomentBayerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def test_all_stencil_endpoints_measure_requested_colour(self):
        pattern=((0,1),(1,2));self.assertEqual(len(STENCILS),8)
        for colour,y,x,supports in STENCILS:
            self.assertEqual(len(supports),4)
            for support in supports:
                self.assertEqual(sum(weight for _,_,weight in support),1.)
                self.assertEqual(sum(dy*weight for dy,_,weight in support),0.)
                self.assertEqual(sum(dx*weight for _,dx,weight in support),0.)
                for dy,dx,_ in support:
                    self.assertLessEqual(max(abs(dy),abs(dx)),3)
                    self.assertEqual(pattern[(y+dy)%2][(x+dx)%2],colour)

    def test_arbitrary_weights_preserve_signed_constants_including_borders(self):
        target=torch.tensor([-.2,.4,1.3])[None,:,None,None].expand(1,3,32,40).clone()
        pattern=torch.tensor([PATTERNS[0]]);packed=sample_rgb(target,pattern)
        coefficients=torch.rand(1,8,4,16,20)*2-1
        torch.testing.assert_close(interpolate_weights(packed,pattern,coefficients),target,rtol=1e-6,atol=1e-7)

    def test_arbitrary_weights_reproduce_affine_fields_away_from_boundary(self):
        y,x=torch.meshgrid(torch.arange(32)/32,torch.arange(40)/40,indexing='ij')
        target=torch.stack((-.2+x*.3+y*.1,.1+x*.2-y*.3,1.1-x*.1+y*.2))[None]
        p=torch.tensor([PATTERNS[0]]);packed=sample_rgb(target,p);coeff=torch.rand(1,8,4,16,20)*2-1
        output=interpolate_weights(packed,p,coeff)
        torch.testing.assert_close(output[:,:,4:-4,4:-4],target[:,:,4:-4,4:-4],rtol=2e-6,atol=5e-7)

    def test_initialization_matches_bilinear_all_phases_and_observations_exact(self):
        torch.manual_seed(71);model=MomentBayerUNet(4);target=torch.rand(1,3,32,40)*1.4-.2
        for phase in PATTERNS:
            p=torch.tensor([phase]);packed=sample_rgb(target,p)
            output=predict(model,packed,torch.zeros_like(packed),p,exact_observations=True,canonical_input=True)
            torch.testing.assert_close(output,bilinear(packed,p),rtol=1e-6,atol=1e-7)
            self.assertTrue(torch.equal(sample_rgb(output,p),packed))

    def test_coefficients_have_finite_gradients_and_noisy_mode_refused(self):
        model=MomentBayerUNet(4);target=torch.rand(1,3,32,32);p=torch.tensor([PATTERNS[0]])
        packed=sample_rgb(target,p);zero=torch.zeros_like(packed)
        model(packed,zero,p,exact_observations=True).square().mean().backward()
        self.assertTrue(all(v.grad is None or torch.isfinite(v.grad).all() for v in model.parameters()))
        self.assertGreater(model.head.weight.grad.abs().sum().item(),0.)
        with self.assertRaises(ValueError):model(packed,zero+.01,p,exact_observations=True)
        with self.assertRaises(ValueError):model(packed,zero,p)
        with self.assertRaises(ValueError):model_from_config(dict(width=4,model_kind='unknown'))

    def test_nonzero_learned_head_preserves_affine_fields_in_all_phases(self):
        model=MomentBayerUNet(4)
        torch.nn.init.normal_(model.head.weight,std=.2);torch.nn.init.uniform_(model.head.bias,-.3,.3)
        y,x=torch.meshgrid(torch.arange(32)/32,torch.arange(40)/40,indexing='ij')
        target=torch.stack((x*.2-y*.1-.1,.1+y*.2+x*.1,1.2-x*.2-y*.1))[None]
        for phase in PATTERNS:
            p=torch.tensor([phase]);packed=sample_rgb(target,p)
            output=predict(model,packed,torch.zeros_like(packed),p,exact_observations=True,canonical_input=True)
            torch.testing.assert_close(output[:,:,4:-4,4:-4],target[:,:,4:-4,4:-4],rtol=3e-6,atol=1e-6)

    def test_invalid_coefficient_field_is_refused(self):
        p=torch.tensor([PATTERNS[0]]);packed=torch.zeros(1,4,16,16)
        for coefficients in (torch.zeros(1,8,3,16,16),torch.ones(1,8,4,16,16)*2):
            with self.assertRaises(ValueError):interpolate_weights(packed,p,coefficients)


if __name__=='__main__':unittest.main()
