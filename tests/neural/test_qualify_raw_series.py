"""Deterministic CFA qualification contracts; no card access required."""
from pathlib import Path
import tempfile
import unittest

import numpy as np

import qualify_raw_series as audit


class RawSeriesTests(unittest.TestCase):
    def test_odd_cfa_geometry_keeps_plane_order_and_bounds(self):
        mosaic=np.zeros((33,35),dtype=np.uint16)
        for i in range(4):
            mosaic[i//2::2,i%2::2]=i+10
        rois=audit.grid_rois(mosaic.shape,side=4)
        tiles=audit.extract_rois(mosaic,rois)
        self.assertEqual(tiles.shape,(9,4,4,4))
        np.testing.assert_array_equal(tiles.mean(axis=(0,2,3)),[10,11,12,13])

    def test_no_temporal_noise_is_inferred_from_static_texture(self):
        texture=np.arange(144,dtype=np.uint16).reshape(1,1,4,6,6)
        rows=audit.temporal_summary(np.repeat(texture,6,axis=0))
        self.assertTrue(all(r['temporal_rms_dn']==0 for r in rows))

    def test_temporal_units_and_interleaved_difference(self):
        data=np.array([0,2,0,2],dtype=np.uint16).reshape(4,1,1,1,1)
        row=audit.temporal_summary(data)[0]
        self.assertAlmostEqual(row['temporal_rms_dn'],np.sqrt(4/3))
        self.assertAlmostEqual(row['adjacent_difference_rms_over_sqrt2_dn'],np.sqrt(2))
        self.assertEqual(row['interleaved_half_mean_rmse_dn'],2)
        self.assertEqual(row['frame_mean_range_dn'],2)

    def test_nonfinite_and_short_series_rejected(self):
        for data in (np.zeros((3,1,1,2,2)),np.full((4,1,1,2,2),np.nan)):
            with self.assertRaises(ValueError):
                audit.temporal_summary(data)

    def test_outside_output_rejected_before_inventory_read(self):
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(ValueError):
                audit.run(Path('missing.json'),Path(folder)/'output')

    def test_out_of_bounds_roi_rejected(self):
        with self.assertRaises(ValueError):
            audit.extract_rois(np.zeros((16,16),dtype=np.uint16),
                [dict(x=7,y=7,width=4,height=4)])


if __name__ == '__main__':
    unittest.main()
