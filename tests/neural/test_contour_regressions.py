import json
import unittest

from audit_contour_regressions import descriptors, summarize, analyze, reject_nonfinite, CONTRAST_NAMES


class ContourRegressionTests(unittest.TestCase):
    def row(self, seed, base, output):
        recipe = dict(low_rgb=[0., 0., 0.], high_rgb=[.2, .2, .2], normal_degrees=20.)
        return dict(seed=seed, phase_name='RGGB', pixels=1, baseline_sse=base, output_sse=output,
            reference_sse=base, baseline_opponent_sse=base, output_opponent_sse=output,
            **recipe, **descriptors(recipe))

    def test_weighted_gain_does_not_hide_majority_regressions(self):
        result = summarize([self.row(1, 100., 90.), self.row(2, 1., 2.), self.row(3, 1., 2.)])
        self.assertAlmostEqual(result['mse_ratio'], 94/102)
        self.assertEqual(result['median_informative_case_ratio'], 2.)
        self.assertEqual((result['improved'], result['worsened'], result['tied']), (1, 2, 0))
        self.assertEqual(result['distinct_contents'], 3)

    def test_contrast_boundaries_and_normal_bins(self):
        for contrast, group in ((0., 0), (.149, 0), (.15, 1), (.35, 2), (.65, 3)):
            result = descriptors(dict(low_rgb=[0.]*3, high_rgb=[contrast]*3, normal_degrees=45.))
            self.assertEqual(result['contrast_bin'], CONTRAST_NAMES[group])
            self.assertEqual(result['normal_bin'], 1)
            self.assertEqual(result['colour_kind'], 'achromatic')
        self.assertEqual(descriptors(dict(low_rgb=[0.]*3, high_rgb=[.2, .3, .4], normal_degrees=179.))['colour_kind'], 'chromatic')

    def test_partitions_preserve_support_and_empty_bins_remain_explicit(self):
        result = analyze([self.row(1, 1., .5), self.row(2, 2., 2.)])
        self.assertEqual(result['all']['pixels'], 2)
        self.assertEqual(result['by_contrast'][CONTRAST_NAMES[1]]['pixels'], 2)
        self.assertIsNone(result['by_contrast'][CONTRAST_NAMES[0]])
        self.assertIsNone(result['by_phase']['BGGR'])
        self.assertIsNone(summarize([]))

    def test_nonfinite_negative_and_empty_support_are_rejected(self):
        for bad in (float('nan'), float('inf'), -1.):
            row = self.row(1, 1., bad)
            with self.assertRaises(ValueError):
                summarize([row])
        with self.assertRaises(ValueError):
            summarize([dict(self.row(1, 1., 1.), pixels=0)])
        with self.assertRaises(ValueError):
            descriptors(dict(low_rgb=[0.]*3, high_rgb=[float('nan')]*3, normal_degrees=0.))
        with self.assertRaises(ValueError):
            json.loads('{"value": NaN}', parse_constant=reject_nonfinite)


if __name__ == '__main__':
    unittest.main()
