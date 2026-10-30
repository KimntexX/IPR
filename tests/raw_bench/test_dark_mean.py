import copy
import unittest
import numpy as np
from audit_dark_mean import coarse_dark_cells, frozen_dark_mask


class FrozenDarkMaskTests(unittest.TestCase):
    def setUp(self):
        self.reference = np.repeat((np.arange(64).reshape(8, 8)*200).astype(np.uint16)[..., None], 3, axis=2)
        self.scene = dict(masks=dict(common_area=dict(lr_origin_xy=[0, 0], size_wh=[8, 8],
                          ipr_origin_xy=[2, 1]), pixels=dict(dark=29),
                          sha256_packbits=dict(dark='84f28852ca47c3b5fd51c11e2c4cb4cc1f25836707d9438f372695c92817b460')),
                          alignment=dict(ipr_size_wh=[12, 10]))

    def test_golden_mask_and_offset(self):
        mask, _ = frozen_dark_mask(self.reference, self.scene)
        expected = np.array([[1]*8, [1]*8, [1]*8, [1, 1, 0, 0, 0, 0, 1, 1],
                             [1, 0, 0, 0, 0, 0, 0, 0], [0]*8, [0]*8, [0]*8], np.uint8)
        np.testing.assert_array_equal(mask[1:9, 2:10], expected)
        self.assertEqual(int(mask.sum()), 29)
        self.assertFalse(mask[0].any())
        self.assertFalse(mask[:, :2].any())

    def test_invalid_image(self):
        for reference in (None, self.reference.astype(np.uint8), self.reference[..., 0]):
            with self.assertRaises(ValueError):
                frozen_dark_mask(reference, self.scene)

    def test_hash_mismatch(self):
        scene = copy.deepcopy(self.scene)
        scene['masks']['sha256_packbits']['dark'] = 'bad'
        with self.assertRaisesRegex(ValueError, 'hash mismatch'):
            frozen_dark_mask(self.reference, scene)

    def test_count_mismatch(self):
        scene = copy.deepcopy(self.scene)
        scene['masks']['pixels']['dark'] = 1
        with self.assertRaisesRegex(ValueError, 'count mismatch'):
            frozen_dark_mask(self.reference, scene)

    def test_reference_area_outside(self):
        scene = copy.deepcopy(self.scene)
        scene['masks']['common_area']['lr_origin_xy'] = [4, 4]
        with self.assertRaisesRegex(ValueError, 'outside image'):
            frozen_dark_mask(self.reference, scene)

    def test_camera_area_outside(self):
        scene = copy.deepcopy(self.scene)
        scene['masks']['common_area']['ipr_origin_xy'] = [11, 9]
        with self.assertRaisesRegex(ValueError, 'outside image'):
            frozen_dark_mask(self.reference, scene)

    def test_coarse_cells_include_bright_pixels(self):
        image=np.full((256,256,3),80*257,np.uint16)
        image[:128,:128]=10*257
        image[:64,:128]=30*257  # Individual bright pixels are not rejected.
        scene=dict(masks=dict(common_area=dict(lr_origin_xy=[0,0],size_wh=[256,256],ipr_origin_xy=[2,3])),
                   alignment=dict(ipr_size_wh=[260,260]))
        mask,cells=coarse_dark_cells(image,scene)
        self.assertEqual(len(cells),1)
        self.assertEqual(cells[0]['camera_roi'],[2,3,128,128])
        self.assertAlmostEqual(cells[0]['lr_mean_luma'],20.,places=4)
        self.assertEqual(int(mask.sum()),128*128)
        self.assertTrue(mask[3,2])

    def test_partial_edge_cells_are_excluded(self):
        image=np.full((130,130,3),10*257,np.uint16)
        scene=dict(masks=dict(common_area=dict(lr_origin_xy=[0,0],size_wh=[130,130],ipr_origin_xy=[0,0])),
                   alignment=dict(ipr_size_wh=[130,130]))
        mask,cells=coarse_dark_cells(image,scene)
        self.assertEqual(len(cells),1)
        self.assertFalse(mask[128:].any())
        self.assertFalse(mask[:,128:].any())

    def test_no_dark_cells_is_not_threshold_retuning(self):
        image=np.full((128,128,3),80*257,np.uint16)
        scene=dict(masks=dict(common_area=dict(lr_origin_xy=[0,0],size_wh=[128,128],ipr_origin_xy=[0,0])),
                   alignment=dict(ipr_size_wh=[128,128]))
        mask,cells=coarse_dark_cells(image,scene)
        self.assertEqual(cells,[])
        self.assertFalse(mask.any())


if __name__ == '__main__':
    unittest.main()
