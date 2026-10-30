import unittest
from generate_noise_oracle import HOOK, MARKER, instrument
from study_noise_model_ablation import encoded, summarize, VARIANTS
import numpy as np


class ModelAblationContracts(unittest.TestCase):
    def test_generated_copy_has_one_hook_and_unchanged_engine_body(self):
        original = 'namespace X {\n'+MARKER+'}\n'
        generated = instrument(original)
        body = generated.split('\n',2)[2]
        self.assertEqual(body.replace(HOOK,''),original)
        self.assertEqual(body.count(HOOK),1)

    def test_boundary_drift_fails_closed(self):
        for text in ('no marker',MARKER+MARKER,'IprNoiseBench\n'+MARKER):
            with self.assertRaises(ValueError):
                instrument(text)

    def test_payload_is_float32_rgb_and_stable(self):
        import base64
        field = np.full((256,256),-.03)
        encoded_field,digest = encoded(field)
        pixels = np.frombuffer(base64.b64decode(encoded_field),dtype='<f4').reshape(256,256,3)
        self.assertEqual(len(digest),64)
        self.assertTrue(np.all(pixels==np.float32(-.03)))
        self.assertEqual(encoded(field)[1],digest)

    def test_summary_keeps_paired_ratios_and_clean_stress(self):
        values = {}
        for index,(_,keys) in enumerate(VARIANTS.items()):
            values[keys[0]] = dict(rgb_mse=float(index+1))
            values[keys[1]] = dict(pattern_gain=.5)
        result = summarize([dict(noise_mode='fixed',pair='dev',pattern='line',measurements=values)])
        self.assertEqual(result[0]['variants']['same_noise']['median_mse_ratio_to_blind_pooled'],1.5)
        self.assertEqual(result[0]['variants']['independent_noise']['median_clean_gain'],.5)


if __name__ == '__main__':
    unittest.main()
