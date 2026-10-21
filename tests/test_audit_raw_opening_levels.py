import unittest
from audit_raw_opening_levels import compare_levels
from audit_dng_baseline import plain


def fixture():
    dng=dict(sub_ifds=[dict(photometric=32803,white_level=110,black_level=[10]*4)],
             ifd0=dict(as_shot_neutral=[.5,1,.25],baseline_exposure=.26))
    trace=dict(before_scale_colors=dict(maximum=200,data_maximum=150,black=0),
               after_process=dict(pre_mul=[2,1,4,1]))
    return dng,trace


class LevelTests(unittest.TestCase):
    def test_byte_CFA_pattern_preserved_as_numeric_array(self):
        self.assertEqual(plain(b'\x00\x01\x01\x02'),[0,1,1,2])

    def test_span_and_WB(self):
        row=compare_levels(*fixture())
        self.assertEqual(row['dng_span'],100)
        self.assertAlmostEqual(row['white_scale_EV_diagnostic'],1)
        self.assertAlmostEqual(row['baseline_plus_white_EV_diagnostic'],1.26)
        self.assertEqual(row['WB_max_relative_error'],0)

    def test_nonuniform_black_preserved_without_scalar_gain(self):
        dng,trace=fixture();dng['sub_ifds'][0]['black_level']=[10,11,10,10]
        result=compare_levels(dng,trace)
        self.assertFalse(result['black_uniform'])
        self.assertIsNone(result['white_scale_gain_diagnostic'])
        self.assertEqual(result['dng_spans_CFA_order'],[100,99,100,100])
        self.assertLess(result['white_scale_EV_bounds_diagnostic'][0],result['white_scale_EV_bounds_diagnostic'][1])

    def test_multiple_RAW_IFDs_refused(self):
        dng,trace=fixture();dng['sub_ifds']*=2
        with self.assertRaisesRegex(ValueError,'one mosaic'):
            compare_levels(dng,trace)

    def test_invalid_span_refused(self):
        dng,trace=fixture();dng['sub_ifds'][0]['white_level']=10
        with self.assertRaisesRegex(ValueError,'Invalid DNG span'):
            compare_levels(dng,trace)


if __name__=='__main__':unittest.main()
