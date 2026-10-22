import unittest
from run_raw_radiometric_trials import aggregate
from compare_raw_radiometric_output import distribution,select_exports
from pathlib import Path
import numpy as np


class AggregationTests(unittest.TestCase):
    def test_duplicate_exports_require_explicit_selection(self):
        paths=[Path('0054-IPR_RAW_Audit.tiff'),Path('0054-IPR_RAW_Audit-2.tiff')]
        with self.assertRaisesRegex(ValueError,'Ambiguous'):select_exports(paths,{})
        selected,candidates=select_exports(paths,{'0054':paths[1].name})
        self.assertEqual(selected['0054'],paths[1])
        self.assertEqual(candidates['0054'],paths)
        with self.assertRaisesRegex(ValueError,'Unknown'):select_exports(paths,{'0054':'missing.tiff'})

    def test_clipping_uses_native_16_bit_threshold_and_step(self):
        pixels=np.zeros((16,16,3),dtype=np.uint16)
        pixels[0,0,0]=65534
        pixels[1,1,:]=65535  # outside the sampled grid
        result=distribution(pixels)
        self.assertEqual(result['samples'],4)
        self.assertEqual(result['encoded_clipped_percent'],25)
        self.assertEqual(result['all_channels_high_percent'],0)

    def test_cohorts_do_not_hide_overcorrection(self):
        def row(camera,current,corrected,clip):
            return dict(camera=camera,models={
                'channel':dict(relative_mean_Y_to_LR_percent=current,encoded_clipped_percent=0),
                'exposure_channel':dict(relative_mean_Y_to_LR_percent=corrected,encoded_clipped_percent=clip)})
        result=aggregate([row('A',-30,-5,.1),row('A',-10,20,.2),row('B',-12,25,.3)])
        self.assertEqual(result['A']['channel']['mean_absolute_Y_error_percent'],20)
        self.assertEqual(result['A']['exposure_channel']['worse_absolute_Y_than_current'],1)
        self.assertEqual(result['B']['exposure_channel']['worse_absolute_Y_than_current'],1)
        self.assertEqual(result['all']['exposure_channel']['scenes'],3)
        self.assertEqual(result['all']['exposure_channel']['max_added_clipping_percentage_points'],.3)

    def test_signed_mean_is_not_absolute_error(self):
        rows=[dict(camera='A',models={'channel':dict(relative_mean_Y_to_LR_percent=value,encoded_clipped_percent=0)}) for value in (-20,20)]
        result=aggregate(rows)['all']['channel']
        self.assertEqual(result['mean_signed_Y_error_percent'],0)
        self.assertEqual(result['mean_absolute_Y_error_percent'],20)


if __name__=='__main__':unittest.main()
