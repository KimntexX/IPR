import copy
import unittest
import numpy as np
from analyse_neutres_rec2020 import ZERO, audit_saved, audit_lr, stats


class NeutralRec2020AuditTests(unittest.TestCase):
    def state(self):
        settings = {k: 0 for k in ZERO}
        settings.update(mixer=[[0, 0, 0] for _ in range(8)],
                        grading=[[240, 0] for _ in range(3)], curves=[[], [], [], []])
        return dict(source_signature_matches=True, settings=settings,
            raw=dict(normalizationEnabled=True, lensCorrectionEnabled=False,
                     chromaticAberrationEnabled=False,
                     engineVersion='ipr-render-rec2020-linear-d65-v1|libraw=test',
                     manifestSha256='known'))

    def test_neutral_and_inactive_grading_hue_accepted(self):
        audit_saved(self.state(), 'known')

    def test_vibrance_and_curves_rejected(self):
        state = self.state()
        state['settings']['vibrance'] = 1
        with self.assertRaises(ValueError): audit_saved(state, 'known')
        state = self.state()
        state['settings']['curves'][0] = [[0, 0], [.5, .7], [1, 1]]
        with self.assertRaises(ValueError): audit_saved(state, 'known')

    def test_optics_calibration_engine_and_manifest_guards(self):
        for name, value in (('lensCorrectionEnabled', True), ('normalizationEnabled', False),
                            ('engineVersion', 'other'), ('manifestSha256', 'modified')):
            state = self.state()
            state['raw'][name] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                audit_saved(state, 'known')

    def test_lr_as_shot_temperature_tint_not_forced_to_zero(self):
        audit_lr({'xmp_settings': dict(CameraProfile='Camera Neutral', WhiteBalance='As Shot',
                                      Temperature='5200', Tint='+5', Vibrance='0')})

    def test_wrong_lr_profile_wb_and_mixer_rejected(self):
        base = {'xmp_settings': dict(CameraProfile='Camera Neutral', WhiteBalance='As Shot')}
        for key, value in (('CameraProfile', 'Adobe Standard'), ('WhiteBalance', 'Custom'),
                           ('HueAdjustmentRed', '1'), ('Sharpness', '40')):
            meta = copy.deepcopy(base)
            meta['xmp_settings'][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): audit_lr(meta)

    def test_black_white_distributions_and_chroma(self):
        for code, luminance in ((0, 0.), (65535, 1.)):
            result = stats(np.full((16, 16, 3), code, dtype=np.uint16))
            self.assertEqual(result['samples'], 4)
            self.assertAlmostEqual(result['mean_Y'], luminance)
            self.assertEqual(result['channel_high_percent'], [100. if code else 0.] * 3)
            self.assertLess(result['mean_chroma_Lab'], .01)


if __name__ == '__main__':
    unittest.main()
