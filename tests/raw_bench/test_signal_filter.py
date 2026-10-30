import base64
import unittest
import numpy as np
from study_signal_filter import fixture,encoded_rgb,summary,PATTERNS,CONTROL_SEEDS,REFERENCE_SEED,VARIANTS


class SignalFilterContracts(unittest.TestCase):
    def test_reference_seed_not_control(self):
        self.assertNotIn(REFERENCE_SEED,CONTROL_SEEDS)

    def test_all_fixtures_positive_bounded_and_deterministic(self):
        for level in (.003,.03,.1):
            for pattern in PATTERNS:
                first=fixture(level,pattern)
                self.assertEqual(first.shape,(256,256))
                self.assertTrue(np.isfinite(first).all())
                self.assertGreater(first.min(),0)
                self.assertLess(np.max(np.abs(first-level)),.001501)
                np.testing.assert_array_equal(first,fixture(level,pattern))

    def test_color_edge_samples_green_both_phases(self):
        cfa=fixture(.03,'color_edge')
        self.assertAlmostEqual(cfa[0,0],.0285,places=7)
        self.assertAlmostEqual(cfa[0,1],.030375,places=7)
        self.assertAlmostEqual(cfa[1,0],.030375,places=7)
        self.assertAlmostEqual(cfa[1,1],.03075,places=7)

    def test_rgb_encoder_preserves_signed_channels(self):
        image=np.zeros((256,256,3));image[...,0]=-.1;image[...,2]=1.2
        text,digest=encoded_rgb(image)
        pixels=np.frombuffer(base64.b64decode(text),dtype='<f4').reshape(image.shape)
        np.testing.assert_array_equal(pixels,image.astype(np.float32))
        self.assertEqual(len(digest),64)
        with self.assertRaises(ValueError):encoded_rgb(image[...,0])

    def test_summary_exposes_worst_regression(self):
        rows=[]
        for error in (1.,3.):
            metrics={key:{'rgb_mse':1.} for key in VARIANTS.values()}
            metrics['independent_noise_pooled_noisy']['rgb_mse']=error
            rows.append(dict(iso=1600,level=.03,pattern='flat',input_rgb_mse=2.,measurements=metrics))
        result=summary(rows)[0]['variants']['signal_profile']
        self.assertEqual(result['median_mse_ratio_to_actual'],2.)
        self.assertEqual(result['worst_mse_ratio_to_actual'],3.)


if __name__=='__main__':unittest.main()
