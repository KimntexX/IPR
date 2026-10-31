"""Synthetic-only normalization, masks and partition checks."""
import unittest

import torch

from camera_data import normalize_pair,restore,masked_mse,validate_splits


class CameraDataTests(unittest.TestCase):
    def setUp(self):
        self.image=torch.linspace(-.2,1.5,3*8*12).reshape(1,3,8,12)
        self.sigma=torch.full_like(self.image,.04)

    def test_roundtrip_and_sigma_units(self):
        before=self.image.clone()
        x,y,s=normalize_pair(self.image,self.image,self.sigma,2.)
        self.assertTrue(torch.equal(restore(x,2.),self.image))
        self.assertTrue(torch.equal(y,x))
        self.assertTrue(torch.equal(s,self.sigma/2))
        self.assertTrue(torch.equal(before,self.image))
        self.assertLess(x.min().item(),0)
        self.assertGreater(restore(x,2.).max().item(),1)

    def test_invalid_scale(self):
        for scale in (0,-1,True,'2',float('nan'),float('inf'),1e100,1e-100):
            with self.assertRaises(ValueError):
                normalize_pair(self.image,self.image,self.sigma,scale)

    def test_roundoff_not_bitwise_identity_for_arbitrary_scale(self):
        for scale in (1.,3.,16.):
            x,_,_=normalize_pair(self.image,self.image,self.sigma,scale)
            self.assertTrue(torch.allclose(restore(x,scale),self.image,atol=2e-7,rtol=2e-7))

    def test_loss_overflow(self):
        x=torch.full_like(self.image,1e30)
        mask=torch.ones((1,1,8,12),dtype=torch.bool)
        with self.assertRaises(ValueError):masked_mse(x,self.image,mask)

    def test_reference_contract(self):
        for target in (None,self.image[:,:,:,:4],self.image.double(),self.image*float('nan')):
            with self.assertRaises(ValueError):
                normalize_pair(self.image,target,self.sigma)

    def test_loss_mask_not_diluted(self):
        x=torch.zeros_like(self.image)
        x[:,:,:,6:]=100
        mask=torch.zeros((1,1,8,12),dtype=torch.bool)
        mask[:,:,:,:6]=True
        self.assertEqual(masked_mse(x,self.image*0,mask).item(),0)
        self.assertEqual(masked_mse(x+1,self.image*0,mask).item(),1)

    def test_loss_gradients_only_on_valid_pixels(self):
        x=torch.ones_like(self.image,requires_grad=True)
        mask=torch.zeros((1,1,8,12),dtype=torch.bool);mask[:,:,:,:6]=True
        masked_mse(x,torch.zeros_like(x),mask).backward()
        self.assertTrue((x.grad[:,:,:,6:]==0).all())
        self.assertTrue((x.grad[:,:,:,:6]>0).all())

    def test_empty_or_wrong_mask(self):
        for mask in (None,torch.zeros((1,1,8,12),dtype=torch.bool),
                     torch.ones((1,1,8,12)),torch.ones((1,3,8,12),dtype=torch.bool)):
            with self.assertRaises(ValueError):
                masked_mse(self.image,self.image,mask)

    def test_nonfinite_masked_pixel_still_rejected(self):
        x=self.image.clone();x[:,:,:,6:]=float('nan')
        mask=torch.zeros((1,1,8,12),dtype=torch.bool);mask[:,:,:,:6]=True
        with self.assertRaises(ValueError):
            masked_mse(x,self.image,mask)

    def rows(self):
        return [dict(id='a',scene_group='scene-a',split='train',content_sha256='a'*64),
                dict(id='b',scene_group='scene-b',split='test',content_sha256='b'*64)]

    def test_valid_partition(self):
        self.assertEqual(validate_splits(self.rows()),dict(train=1,validation=0,test=1))

    def test_scene_leakage_despite_different_content(self):
        rows=self.rows();rows[1]['scene_group']='scene-a'
        with self.assertRaises(ValueError):validate_splits(rows)

    def test_duplicate_content_despite_different_scene(self):
        rows=self.rows();rows[1]['content_sha256']='a'*64
        with self.assertRaises(ValueError):validate_splits(rows)

    def test_bad_manifest(self):
        for key,value in (('id','a'),('split','holdout'),('scene_group',''),('content_sha256','zz')):
            rows=self.rows();rows[1][key]=value
            with self.assertRaises(ValueError):validate_splits(rows)


if __name__=='__main__':
    torch.set_num_threads(2)
    unittest.main()
