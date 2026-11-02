import unittest
import torch
from torch.nn import functional as F

from bayer_canonical import canonicalize,spatial_reflect,predict
from joint_bayer_unet import JointBayerUNet,sample_rgb,bilinear
from synthetic_bayer_data import PATTERNS


class BayerCanonicalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def fixture(self):
        patterns=list(PATTERNS)+[tuple(3 if c==1 else 1 if c==3 else c for c in p) for p in PATTERNS]
        pattern=torch.tensor(patterns)
        rgb=torch.linspace(-.3,1.4,8*3*32*40).reshape(8,3,32,40)
        packed=sample_rgb(rgb,pattern)
        sigma=torch.arange(packed.numel()).reshape_as(packed).float()/1e6
        return rgb,packed,sigma,pattern

    def test_signed_non_square_roundtrip_exact_all_phases_and_green_labels(self):
        rgb,packed,sigma,pattern=self.fixture()
        cp,cs,labels,flags=canonicalize(packed,sigma,pattern)
        restored=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(cp,2),flags),2)
        restored_sigma=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(cs,2),flags),2)
        self.assertTrue(torch.equal(restored,packed));self.assertTrue(torch.equal(restored_sigma,sigma))
        self.assertTrue(torch.equal(spatial_reflect(labels.reshape(-1,1,2,2),flags).reshape(-1,4),pattern))
        self.assertTrue(torch.equal(sample_rgb(spatial_reflect(rgb,flags),labels),cp))
        self.assertTrue((labels[:,0]==0).all());self.assertTrue((labels[:,3]==2).all())

    def test_bilinear_and_exact_sensor_samples_survive_coordinate_transform(self):
        rgb,packed,_,pattern=self.fixture();sigma=torch.zeros_like(packed);model=JointBayerUNet(4)
        output=predict(model,packed,sigma,pattern,exact_observations=True,canonical_input=True)
        torch.testing.assert_close(output,bilinear(packed,pattern),rtol=1e-6,atol=1e-7)
        self.assertTrue(torch.equal(sample_rgb(output,pattern),packed))

    def test_gradients_survive_inverse_and_no_source_changes(self):
        _,packed,_,pattern=self.fixture();before=packed.clone();model=JointBayerUNet(4)
        output=predict(model,packed,torch.zeros_like(packed),pattern,exact_observations=True,canonical_input=True)
        output.square().mean().backward()
        self.assertTrue(all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()))
        self.assertTrue(torch.equal(before,packed))

    def test_non_bayer_corner_arrangement_rejected(self):
        packed=torch.zeros(1,4,16,16)
        with self.assertRaises(ValueError):canonicalize(packed,packed,torch.tensor([[0,2,1,3]]))


if __name__=='__main__':unittest.main()
