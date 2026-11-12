"""Synthetic contracts for motion, radiometry and pair validity, not photo tuning."""
from pathlib import Path
import tempfile
import unittest

import cv2
import numpy as np

from photo_pair_math import (balanced_gains,sigma_proxy,translation,warp,
    reference_tile,affine_fit,tile_quality,patches)
from qualify_photo_pairs import read_plane
from run_photo_pilot import capture_selection,checked_output


PROTOCOL=dict(reference_lowpass_sigma=4.,reference_stability_dn_fraction=.001,
    unsaturated_camera_fraction=.9,minimum_reference_valid_fraction=.5,
    tile_radiometry_floor=.003,tile_radiometry_noise_multiplier=6.,
    minimum_patch_valid_fraction=.9,patch_origins=[24,144,264])


class PhotoPairTests(unittest.TestCase):
    def test_selection_preserves_reference_bursts_and_excludes_isolated(self):
        rows=capture_selection()
        self.assertEqual(len(rows),69)
        self.assertEqual(sum(g in (2,7) for g,_,_ in rows),51)
        self.assertNotIn(210,[n for _,_,n in rows])
        self.assertEqual(len({n for _,_,n in rows}),69)

    def test_output_is_confined(self):
        with tempfile.TemporaryDirectory() as temporary,self.assertRaises(ValueError):
            checked_output(Path(temporary)/'output')

    def test_balanced_gains_reconstruct_minimum_one(self):
        np.testing.assert_allclose(balanced_gains([.5,1.,.8]),[2,1,1.25])
        np.testing.assert_allclose(balanced_gains([2.,1.,.5]),[1,2,4])
        for neutral in ([0,1,1],[1,float('nan'),1],[1,1]):
            with self.assertRaises(ValueError):balanced_gains(neutral)

    def test_sigma_is_single_image_proxy_and_does_not_clamp(self):
        rng=np.random.default_rng(71)
        image=(rng.normal(0,.02,(200,200,3))-.1).astype(np.float32)
        before=image.copy();estimated=sigma_proxy(image)
        np.testing.assert_allclose(estimated,.02,rtol=.06)
        np.testing.assert_array_equal(image,before)
        np.testing.assert_array_equal(sigma_proxy(np.ones((8,8,3),np.float32)),np.full(3,1e-6,np.float32))
        image[0,0,0]=np.nan
        with self.assertRaises(ValueError):sigma_proxy(image)

    def test_translation_direction_and_sensor_units(self):
        rng=np.random.default_rng(92)
        reference=cv2.GaussianBlur(rng.random((128,128),dtype=np.float32),(0,0),1.)
        source=warp(reference,[2.,-1.])
        found=translation(reference,source,[2.,2.])
        self.assertTrue(found['accepted'])
        np.testing.assert_allclose(found['shift_xy'],[4,-2],atol=.15)
        recovered=warp(source,[2,-1],to_reference=True)
        np.testing.assert_allclose(recovered[8:-8,8:-8],reference[8:-8,8:-8],atol=1e-6)

    def test_unobservable_registration_is_not_invented(self):
        image=np.ones((64,64),np.float32)
        self.assertFalse(translation(image,image,[1,1])['accepted'])
        with self.assertRaises(ValueError):translation(image,image[:32],[1,1])

    def test_post_white_balance_clipping_is_not_hidden_by_gain(self):
        source=np.full((96,96,3),.99,np.float32)
        valid,summary=tile_quality(source,source,np.ones((96,96),bool),np.full(3,2.),np.full(3,.01),PROTOCOL)
        self.assertFalse(summary['accepted'])
        self.assertFalse(valid.any())

    def test_saturated_registration_has_no_fabricated_solution(self):
        image=np.ones((64,64),np.float32)
        image[20:40,20:40]=1.1
        self.assertFalse(translation(image,image,[1,1])['accepted'])

    def test_reference_retains_signed_values_and_rejects_local_light_variation(self):
        base=np.full((96,96,3),-.01,np.float32)
        frames=[]
        for i in range(8):
            frame=base.copy();frame[:,:48]+=.02*i
            frames.append(frame)
        mean,valid,summary=reference_tile(frames,[[0,0]]*8,[np.ones(3,np.float32)]*8,PROTOCOL)
        self.assertLess(mean[48,75,0],0)
        self.assertFalse(valid[48,30]);self.assertTrue(valid[48,75])
        self.assertLess(summary['stable_fraction'],.7)
        with self.assertRaises(ValueError):reference_tile(frames[:3],[[0,0]]*3,[np.ones(3)]*3,PROTOCOL)

    def test_global_radiometry_ignores_monitoring_rois(self):
        yy,xx=np.mgrid[:48,:48]
        base=np.stack([.02+xx/100.,.03+yy/100.,.02+(xx+yy)/200.],axis=-1).astype(np.float32)
        refs=[base.copy() for _ in range(9)]
        gain=np.array([1.2,1.5,1.8],np.float32);offset=np.array([.01,-.01,.02],np.float32)
        noisy=[r*gain+offset for r in refs]
        for i in (1,3,5,7):noisy[i]+=.2
        fitted,bias=affine_fit(refs,noisy,[np.ones((48,48),bool)]*9,[0,2,4,6,8])
        np.testing.assert_allclose(fitted,gain,atol=2e-6)
        np.testing.assert_allclose(bias,offset,atol=2e-6)
        with self.assertRaises(ValueError):affine_fit(refs,noisy,[np.zeros((48,48),bool)]*9,[0,2,4,6,8])

    def test_pair_gate_does_not_reject_negative_noise_or_modify_source(self):
        source=np.full((96,96,3),-.01,np.float32);before=source.copy()
        valid,summary=tile_quality(source,source,np.ones((96,96),bool),np.ones(3),np.full(3,.01),PROTOCOL)
        self.assertTrue(summary['accepted']);self.assertTrue(valid[48,48])
        np.testing.assert_array_equal(source,before)
        _,bad=tile_quality(source,source+.1,np.ones((96,96),bool),np.ones(3),np.full(3,.001),PROTOCOL)
        self.assertFalse(bad['accepted'])

    def test_patches_use_fixed_positions_and_reference_mask(self):
        source=np.full((384,384,3),-.02,np.float32)
        good=list(patches(source,source,np.ones((384,384),bool),np.ones(3),PROTOCOL))
        self.assertEqual(len(good),9);self.assertEqual(good[0]['signal'].shape,(3,96,96))
        self.assertTrue(np.all(good[0]['signal']<0))
        self.assertEqual(list(patches(source,source,np.zeros((384,384),bool),np.ones(3),PROTOCOL)),[])

    def test_bad_capture_payload_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);(folder/'capture.bin').write_bytes(b'abc')
            descriptor=dict(dtype='<f4',shape=[4,4,3],offset_bytes=0)
            with self.assertRaises(ValueError):read_plane(dict(descriptor=str(folder/'capture.json'),pixels='capture.bin'),descriptor)
            with self.assertRaises(ValueError):read_plane(dict(descriptor=str(folder/'capture.json'),pixels='../escape.bin'),descriptor)


if __name__=='__main__':
    unittest.main()
