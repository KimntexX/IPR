import unittest
import numpy as np

from mosaic_data import pack_mosaic,unpack_mosaic,pattern_gains
from qualify_mosaic_inlet import selected_captures


class MosaicDataTests(unittest.TestCase):
    def test_signed_overwhite_roundtrip_exact_and_source_unchanged(self):
        image=np.arange(48,dtype=np.float32).reshape(6,8)/10-2
        before=image.copy();planes=pack_mosaic(image)
        self.assertEqual(planes.shape,(4,3,4))
        np.testing.assert_array_equal(planes[1],image[::2,1::2])
        np.testing.assert_array_equal(unpack_mosaic(planes),image)
        np.testing.assert_array_equal(image,before)

    def test_invalid_mosaics_refused(self):
        for image in (np.zeros((5,4),np.float32),np.zeros((4,4),np.uint16),np.full((4,4),np.nan,np.float32)):
            with self.assertRaises(ValueError):pack_mosaic(image)
        with self.assertRaises(ValueError):unpack_mosaic(np.zeros((3,4,4),np.float32))

    def test_all_bayer_phases_keep_distinct_green_gains(self):
        base=np.array([[0,1],[3,2]])
        for y in (0,1):
            for x in (0,1):
                pattern=np.roll(base,(y,x),(0,1))
                np.testing.assert_array_equal(pattern_gains(pattern,[2,1,3,1.1]),
                    np.array([2,1,3,1.1],np.float32)[pattern.reshape(4)])

    def test_invalid_pattern_or_gain_refused(self):
        for pattern in ([[0,1],[1,1]],[[0,2],[1,4]],[[0.,1.],[3.,2.]]):
            with self.assertRaises(ValueError):pattern_gains(pattern,[2,1,3,1])
        with self.assertRaises(ValueError):pattern_gains([[0,1],[3,2]],[2,0,3,1])

    def test_frozen_selection_does_not_accept_homonym_from_other_scene(self):
        rows=[dict(source=n,iso=iso,scene=scene) for n,iso,scene in
            [('6V1A0134.CR3',100,'indoor'),('6V1A0181.CR3',12800,'indoor')]]
        burst=dict(captures=[dict(source='6V1A9819.CR3',iso=4000,scene='burst_4000_training_candidate')])
        self.assertEqual(len(selected_captures(dict(extraction=dict(records=rows)),burst)),3)
        rows[0]['scene']='control_indoor'
        with self.assertRaises(ValueError):selected_captures(dict(extraction=dict(records=rows)),burst)


if __name__=='__main__':unittest.main()
