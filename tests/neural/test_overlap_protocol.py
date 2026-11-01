"""Common-support protocol contracts independent of real-photo outcomes."""
from copy import deepcopy
import unittest
from unittest.mock import patch
import cv2
import numpy as np

from photo_pair_math import translation,warp,tile_quality
from qualify_photo_pairs import overlap_protocol
from test_photo_pairs import PROTOCOL


class OverlapTests(unittest.TestCase):
    def proxy(self):
        rng=np.random.default_rng(77)
        return cv2.GaussianBlur(rng.uniform(.05,.5,(128,128)).astype(np.float32),(0,0),1.)

    def matrices(self,forward,backward):
        return [(1.,np.array([[1,0,forward],[0,1,0]],np.float32)),
                (1.,np.array([[1,0,backward],[0,1,0]],np.float32))]

    def test_large_sensor_shift_is_admitted_by_real_support(self):
        reference=self.proxy();source=warp(reference,[4,-2])
        result=translation(reference,source,[20.,20.])
        self.assertTrue(result['accepted'])
        np.testing.assert_allclose(result['shift_xy'],[80,-40],atol=1.)
        self.assertGreater(result['full_frame_overlap_fraction'],.9)
        self.assertLessEqual(result['reciprocal_error_pixels'],1.)

    def test_high_correlation_cannot_override_missing_overlap(self):
        image=self.proxy()
        with patch('photo_pair_math.cv2.findTransformECC',side_effect=self.matrices(70,-70)):
            result=translation(image,image,[1,1])
        self.assertFalse(result['accepted']);self.assertLess(result['full_frame_overlap_fraction'],.5)

    def test_nonreciprocal_solution_is_rejected(self):
        image=self.proxy()
        with patch('photo_pair_math.cv2.findTransformECC',side_effect=self.matrices(4,-1)):
            result=translation(image,image,[1,1])
        self.assertFalse(result['accepted']);self.assertEqual(result['reciprocal_error_pixels'],3.)

    def test_low_reverse_correlation_is_rejected(self):
        image=self.proxy();results=self.matrices(4,-4);results[1]=(.8,results[1][1])
        with patch('photo_pair_math.cv2.findTransformECC',side_effect=results):
            self.assertFalse(translation(image,image,[1,1])['accepted'])

    def test_bilinear_mask_requires_every_neighbor(self):
        mask=np.ones((64,64),bool);mask[:,10]=False;before=mask.copy()
        result=warp(mask,[.5,0],mask=True)>0
        self.assertFalse(result[30,10]);self.assertFalse(result[30,11])
        self.assertFalse(result[30,0]);self.assertTrue(result[30,20])
        np.testing.assert_array_equal(mask,before)

    def test_lack_of_local_support_still_rejects_pair(self):
        source=np.full((96,96,3),.1,np.float32)
        mask=np.zeros((96,96),bool);mask[40:48,40:48]=True
        _,quality=tile_quality(source,source,mask,np.ones(3),np.full(3,.001),PROTOCOL)
        self.assertFalse(quality['accepted'])

    def test_protocol_upgrade_preserves_all_photometric_gates(self):
        baseline=dict(PROTOCOL,ecc_floor=.97,max_translation_pixels=12.,minimum_affine_gain=.1,
                      maximum_affine_gain=16.,maximum_affine_offset=.05)
        before=deepcopy(baseline);current=overlap_protocol(baseline)
        self.assertEqual(baseline,before);self.assertNotIn('max_translation_pixels',current)
        for key,value in baseline.items():
            if key!='max_translation_pixels':self.assertEqual(current[key],value)
        self.assertEqual(current['minimum_full_frame_overlap_fraction'],.5)
        self.assertEqual(current['reciprocal_max_error_pixels'],1.)


if __name__=='__main__':unittest.main()
