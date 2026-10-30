import copy
import unittest
import numpy as np
from audit_dark_mean import frozen_dark_mask


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


if __name__ == '__main__':
    unittest.main()
