import unittest

import numpy as np
import torch

from edge_protected_noise import EdgeProtectedNoisyUNet
from photo_cfa_tiling import tiled_correct,correct_window,orient_image


class PhotoCfaTilingTests(unittest.TestCase):
    def test_constant_map_matches_scalar_and_varying_map_preserves_context(self):
        torch.set_num_threads(2);torch.manual_seed(71)
        model=EdgeProtectedNoisyUNet(4).eval()
        with torch.no_grad():model.noise_head.weight.normal_(0.,.03)
        source=np.random.default_rng(42).normal(.2,.01,(67,71)).astype(np.float32)
        a=np.empty_like(source);b=np.empty_like(source)
        sigma=np.full_like(source,.01)
        tiled_correct(model,source,[2,3,1,0],.01,a,core=64,batch=1)
        tiled_correct(model,source,[2,3,1,0],sigma,b,core=64,batch=1)
        self.assertTrue(np.array_equal(a,b))
        sigma*=np.linspace(.5,2,len(source),dtype=np.float32)[:,None]
        tiled_correct(model,source,[2,3,1,0],sigma,b,core=64,batch=1)
        pad=((128,189),(128,185))
        with torch.inference_mode():
            whole=correct_window(model,np.pad(source,pad,mode='reflect')[None],[2,3,1,0],np.pad(sigma,pad,mode='reflect')[None],'cpu')[0]
        self.assertLessEqual(float(np.max(np.abs(b-whole[128:195,128:199]))),2e-6)
        with self.assertRaises(ValueError):
            tiled_correct(model,source,[2,3,1,0],sigma[:-1],b,core=64)
        for invalid in (-1.,float('nan')):
            with self.assertRaises(ValueError):
                tiled_correct(model,source,[2,3,1,0],np.full_like(sigma,invalid),b,core=64)

    def test_odd_extent_zero_noise_and_colour_phase_preserved(self):
        torch.set_num_threads(2)
        model=EdgeProtectedNoisyUNet(4).eval()
        source=np.random.default_rng(12).normal(.2,.02,(35,47)).astype(np.float32)
        output=np.empty_like(source)
        original=source.copy()
        for pattern in ([0,1,3,2],[2,3,1,0]):
            tiled_correct(model,source,pattern,0.,output,core=64,batch=1)
            self.assertTrue(np.array_equal(source,output))
        self.assertTrue(np.array_equal(source,original))

    def test_nonzero_tiled_core_matches_whole_virtual_context(self):
        torch.set_num_threads(2)
        torch.manual_seed(77)
        model=EdgeProtectedNoisyUNet(4).eval()
        with torch.no_grad():
            model.noise_head.weight.normal_(0.,.03)
            model.noise_features.statistics.weight.normal_(0.,.01)
        source=np.random.default_rng(13).normal(.2,.01,(137,145)).astype(np.float32)
        output=np.empty_like(source)
        tiled_correct(model,source,[2,3,1,0],.01,output,core=128,batch=1)
        padded=np.pad(source,((128,128+119),(128,128+111)),mode='reflect')
        with torch.inference_mode():
            whole=correct_window(model,padded[None],[2,3,1,0],.01,'cpu')[0]
        self.assertLessEqual(float(np.max(np.abs(output-whole[128:265,128:273]))),2e-6)

    def test_orientation_order_and_invalid_halo(self):
        image=np.arange(12).reshape(3,4)
        self.assertTrue(np.array_equal(orient_image(image,5),image[:,::-1].T))
        with self.assertRaises(ValueError):
            orient_image(image,8)
        with self.assertRaises(ValueError):
            tiled_correct(None,image.astype(np.float32),[0,1,3,2],0.,image.astype(np.float32),halo=96)
