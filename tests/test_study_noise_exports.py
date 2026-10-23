import unittest
import numpy as np

from study_noise_exports import metrics, matched_roi, compare


class ExportNoiseTests(unittest.TestCase):
    def test_constant_has_zero_high_and_medium_band_proxies(self):
        values=metrics(np.full((32,32,3),.25,dtype=np.float32))
        self.assertEqual(values['sigma_Y'],0)
        self.assertEqual(values['medium_band_Y_mad'],0)
        self.assertEqual(values['medium_band_chroma_mad'],0)

    def test_integer_translation_without_resampling(self):
        rng=np.random.default_rng(20)
        reference=rng.integers(10000,50000,(160,160,3),dtype=np.uint16)
        target=np.roll(reference,(2,3),axis=(0,1))
        roi=dict(top=48,left=48,size=64,kind='flat')
        actual,audit=matched_roi(reference,target,roi,margin=8)
        self.assertEqual((actual['top'],actual['left']),(50,51))
        self.assertEqual(audit['status'],'accepted_integer_translation')

    def test_identical_patch_ratios_are_one(self):
        rng=np.random.default_rng(33)
        reference=rng.integers(10000,50000,(160,160,3),dtype=np.uint16)
        result=compare(reference,reference,[dict(top=48,left=48,size=64,kind='flat')])
        self.assertEqual(result['summary']['flat']['accepted_regions'],1)
        self.assertAlmostEqual(result['summary']['flat']['median_target_to_reference']['sigma_Y'],1)

    def test_uniform_patch_is_not_registered_by_inventing_correspondence(self):
        reference=np.full((80,80,3),30000,dtype=np.uint16)
        result=compare(reference,reference,[dict(top=8,left=8,size=64,kind='flat')])
        self.assertEqual(result['summary']['flat']['accepted_regions'],0)
        self.assertIsNone(result['summary']['flat']['median_target_to_reference']['sigma_Y'])


if __name__=='__main__':
    unittest.main()
