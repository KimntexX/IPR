import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import summarize_dru_data_comparison as report


class PairedResultsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.campaign=self.root/'campaign'
        (self.root/'docs/validation/raw').mkdir(parents=True)
        for arm in ('reference','noise','details'):
            for seed in (71,73):
                folder=self.campaign/arm/f'seed{seed}';folder.mkdir(parents=True)
                (folder/'result.json').write_text(json.dumps(dict(status='completed_dru_bootstrap_not_photographic_qualification',
                    completed_steps=256,initial_model_fingerprint=f'initial-{seed}')),encoding='utf-8')
                for step in (0,128,256):
                    delta=0 if step==0 else dict(reference=0,noise=-.2,details=.2)[arm]
                    group=dict(frequency=10,chromatic=False,noise_index=1,fields=36,
                        mean_sensor_gain_by_colour=[1+delta]*4,sensor_mse_ratio=1+delta)
                    (folder/f'control-{step:06d}.json').write_text(json.dumps(dict(groups=[group],minimum_group_channel_gain=1+delta)),encoding='utf-8')

    def execute(self):
        with patch.object(report,'__file__',str(self.root/'tests/neural/summarize_dru_data_comparison.py')):
            report.run(self.campaign)

    def test_all_arms_milestones_retained_and_paired_deltas_have_correct_sign(self):
        self.execute();result=json.loads((self.campaign/'assessment.json').read_text(encoding='utf-8'))
        self.assertEqual(len(result['records']),18);self.assertEqual(len(result['paired_effects']),8)
        self.assertTrue(all(c['initial_models_bit_exact'] and c['initial_controls_equal'] for c in result['initialization_checks']))
        for effect in result['paired_effects']:
            self.assertAlmostEqual(effect['mse_ratio_delta'],-.2 if effect['arm']=='noise' else .2)

    def test_mismatched_initial_weights_block_conclusion(self):
        path=self.campaign/'noise/seed71/result.json';value=json.loads(path.read_text());value['initial_model_fingerprint']='different'
        path.write_text(json.dumps(value),encoding='utf-8')
        with self.assertRaises(ValueError):self.execute()
        self.assertFalse((self.campaign/'assessment.json').exists())

    def test_changed_control_inputs_block_conclusion(self):
        path=self.campaign/'details/seed73/control-000000.json';value=json.loads(path.read_text());value['changed_observation']='not the same'
        path.write_text(json.dumps(value),encoding='utf-8')
        with self.assertRaises(ValueError):self.execute()
        self.assertFalse((self.campaign/'assessment.json').exists())


if __name__=='__main__':unittest.main()
