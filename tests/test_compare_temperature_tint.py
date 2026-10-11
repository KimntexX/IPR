import unittest
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
import numpy as np
from compare_temperature_tint import pairings, records, neutral_paths, neutral_audit, memory_records


class WhiteBalanceComparisonTests(unittest.TestCase):
    def record(self, engine, value, response, status='measured'):
        return {'scene': 'S', 'engine': engine, 'feature': 'temperature', 'value': value,
                'status': status, 'metadata': {'dimensions': [100, 101]},
                'metrics': {'delta_log_R_B': response, 'axis_samples': 5,
                            'channel_mean_RGB_variant': [.1, .2, .3],
                            'channel_mean_RGB_neutral': [.2, .2, .2],
                            'Y_ratio': 1, 'clipped_percent_variant': 0}}

    def test_pairing_preserves_duplicates_and_does_not_claim_alignment(self):
        pairs, totals = pairings([self.record('IPR', 2000, -2),
                                 self.record('LR', 2000, -3), self.record('LR', 2000, -3)])
        self.assertEqual(pairs[0]['IPR_minus_LR'], 1)
        self.assertEqual(pairs[0]['duplicate_LR_count'], 2)
        self.assertFalse(pairs[0]['cross_engine_pixel_alignment'])
        self.assertEqual(totals[0]['scenes'], 0)
        self.assertEqual(pairs[0]['photometry']['IPR']['channel_ratio_of_means_RGB'], [.5, 1, 1.4999999999999998])

    def test_empty_mask_and_excluded_state_are_not_fabricated(self):
        pairs, totals = pairings([self.record('IPR', 2000, None), self.record('LR', 2000, -3),
                                 self.record('IPR', 3500, -1, 'excluded'), self.record('LR', 3500, -2)])
        self.assertEqual(len(pairs), 1)
        self.assertIsNone(pairs[0]['IPR_minus_LR'])
        self.assertEqual(totals[0]['scenes'], 0)

    def test_diagnostic_pair_does_not_enter_strict_aggregate(self):
        pairs, totals = pairings([self.record('IPR', 2000, -2),
                                 self.record('LR', 2000, -3, 'diagnostic')])
        self.assertEqual(len(pairs), 1)
        self.assertEqual(totals[0]['scenes'], 0)
        self.assertIsNone(totals[0]['IPR_minus_LR_median'])

    def test_historical_reuse_keeps_pixel_hashes_and_is_not_a_fresh_render(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = {'campaign_id': 'proof-v001', 'records': [{
                'scene': 'S', 'engine': 'IPR', 'feature': 'temperature', 'value': 2000,
                'file': str(root / 'state.tiff'), 'reference': str(root / 'support.tiff'),
                'settings': {'temperature': 2000}, 'reference_settings': {'temperature': 5200},
                'metadata': {'sha256': 'original-pixel-hash'},
                'reference_metadata': {'sha256': 'original-support-hash'},
                'provenance': {'executable_sha256': 'original-executable'}}]}
            path = root / 'proof.json'
            with path.open('w', encoding='utf8') as stream:
                json.dump(document, stream)
            item = memory_records([path])[0]
            self.assertEqual(item['expected_sha256'], 'original-pixel-hash')
            self.assertEqual(item['reference_sha256'], 'original-support-hash')
            self.assertFalse(item['provenance']['fresh_IPR_render'])
            self.assertEqual(item['provenance']['original_provenance']['executable_sha256'], 'original-executable')

    def test_duplicate_ipr_pair_does_not_enter_strict_aggregate(self):
        pairs, totals = pairings([self.record('IPR', 2000, -2), self.record('IPR', 2000, -2),
                                 self.record('LR', 2000, -3)])
        self.assertEqual(pairs[0]['duplicate_IPR_count'], 2)
        self.assertEqual(totals[0]['scenes'], 0)

    def test_shared_lr_neutral_uses_raw_name_not_filename(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'arbitrary-name.tif').touch()
            (root / 'actual-scene-IPR.tiff').touch()
            (root / 'actual-scene-IPR-CameraNeutral-AsShot.tiff').touch()
            with patch('compare_temperature_tint.xmp', return_value=self.settings()):
                scenes = neutral_paths(root)
            self.assertEqual(list(scenes), ['actual-scene'])
            self.assertEqual(len(scenes['actual-scene']['IPR']), 2)
            self.assertEqual(scenes['actual-scene']['LR'][0].name, 'arbitrary-name.tif')

    def test_shared_lr_neutral_without_raw_name_is_not_guessed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'suggestive-LR.tif').touch()
            with patch('compare_temperature_tint.xmp', return_value={}):
                with self.assertRaisesRegex(ValueError, 'RawFileName'):
                    neutral_paths(root)

    def test_as_shot_plus_five_is_valid_but_optics_mismatch_stays_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'neutre').mkdir()
            (root / 'couleur' / 'nuance').mkdir(parents=True)
            lr = root / 'neutre' / 'unlabelled.tif'
            ipr = root / 'neutre' / 'actual-scene-IPR-CameraNeutral-AsShot.tiff'
            variant = root / 'couleur' / 'nuance' / 'variant.tif'
            for path in (lr, ipr, variant):
                path.touch()
            baseline = {**self.settings(), 'WhiteBalance': 'As Shot', 'LensProfileEnable': '1'}
            changed = {**self.settings(), 'Tint': '+25', 'WhiteBalance': 'Custom'}
            def inspect(path):
                return np.full((2, 2, 3), 32768, dtype=np.uint16), {
                    'sha256': path.name, 'xmp_settings': baseline if path == lr else {}}, (0, 0)
            with patch('compare_temperature_tint.xmp', side_effect=lambda p: baseline if p == lr else changed), \
                    patch('compare_temperature_tint.native.inspect', side_effect=inspect), \
                    patch('compare_temperature_tint.native.sha256', return_value='variant-hash'), \
                    patch('compare_temperature_tint.native.verify_unchanged'):
                audit = neutral_audit(root)
            row = audit['scenes'][0]
            self.assertEqual(row['lr_as_shot']['Tint'], '+5')
            self.assertNotIn('shared_LR_neutral_not_As_Shot', row['issues'])
            self.assertEqual(row['variants'][0]['shared_neutral_mismatches'], ['LensProfileEnable'])
            self.assertEqual(row['variants'][0]['status'], 'diagnostic')
            self.assertIn(0, row['coverage']['tint']['missing_values'])
            self.assertEqual(row['ipr_selected_reference'], str(ipr.resolve()))
            self.assertFalse(audit['cleanup_eligible'])

    def settings(self, temperature=5200):
        fields = ['Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012',
                  'Whites2012', 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze',
                  'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing',
                  'ColorNoiseReduction', 'GrainAmount', 'AutoLateralCA', 'LensProfileEnable']
        return {**dict.fromkeys(fields, '0'), 'CameraProfile': 'Camera Neutral',
                'RawFileName': 'actual-scene.CR3', 'Temperature': str(temperature), 'Tint': '+5'}

    def test_embedded_raw_name_and_secondary_controls_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'temperature').mkdir()
            anchor, variant = root / 'temperature' / 'misleading.tif', root / 'temperature' / 'other.tif'
            anchor.touch()
            variant.touch()
            settings = {anchor: self.settings(), variant: self.settings(2000)}
            settings[variant]['LensProfileEnable'] = '1'
            with patch('compare_temperature_tint.memory_records', return_value=[]), \
                    patch('compare_temperature_tint.xmp', side_effect=lambda p: settings[p]):
                result, inventory = records(root, [], selected=['temperature'])
            self.assertEqual(len(inventory), 2)
            item = next(r for r in result if r['value'] == 2000)
            self.assertEqual(item['scene'], 'actual-scene')
            self.assertEqual(Path(item['reference']), anchor)
            self.assertIn('secondary_LR_control_nonzero_or_unknown:LensProfileEnable', item['preflight_issues'])

    def test_missing_manual_anchor_is_inventory_only_without_inventing_reference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'temperature').mkdir()
            (root / 'temperature' / 'state.tif').touch()
            with patch('compare_temperature_tint.memory_records', return_value=[]), \
                    patch('compare_temperature_tint.xmp', return_value=self.settings(2000)):
                result, inventory = records(root, [], selected=['temperature'])
            self.assertEqual(result, [])
            self.assertEqual(inventory[0]['excluded_reason'], 'missing_manual_support')


if __name__ == '__main__':
    unittest.main()
