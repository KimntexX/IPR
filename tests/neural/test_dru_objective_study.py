import json
from pathlib import Path
import tempfile
import unittest
from run_dru_objective_study import summarize


class ObjectiveStudyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        records=[dict(observation_sha256=str(i),sigma_sha256='sigma-'+str(i)) for i in range(864)]
        for arm in ('reference','structured'):
            for seed in (71,73):
                folder=self.root/arm/f'seed{seed}';folder.mkdir(parents=True)
                (folder/'result.json').write_text(json.dumps(dict(initial_model_fingerprint=str(seed))),encoding='utf-8')
                for step in (0,128,256):
                    gain=1. if step==0 else (.8 if arm=='reference' else .98)
                    group=dict(frequency=26,chromatic=False,noise_index=1,fields=864,
                        mean_sensor_gain_by_colour=[gain]*4,sensor_mse_ratio=.8)
                    (folder/f'control-{step:06d}.json').write_text(json.dumps(dict(groups=[group],records=records)),encoding='utf-8')

    def test_all_seeds_and_milestones_retained_and_deltas_are_paired(self):
        result=summarize(self.root,self.root)
        self.assertEqual(len(result['rows']),12)
        self.assertEqual(len(result['paired_effects']),4)
        self.assertTrue(result['strict_both_seeds'])
        self.assertTrue(result['promising_both_seeds'])
        for row in result['paired_effects']:
            self.assertAlmostEqual(row['gain_delta'][0],.18)
            self.assertEqual(row['mse_ratio_delta'],0.)

    def test_changed_initialization_refuses_assessment(self):
        p=self.root/'structured/seed73/result.json'
        p.write_text(json.dumps(dict(initial_model_fingerprint='unpaired')),encoding='utf-8')
        with self.assertRaises(ValueError):summarize(self.root,self.root)

    def test_duplicated_observation_refuses_assessment(self):
        p=self.root/'structured/seed71/control-000256.json'
        value=json.loads(p.read_text(encoding='utf-8'));value['records'][1]=value['records'][0]
        p.write_text(json.dumps(value),encoding='utf-8')
        with self.assertRaises(ValueError):summarize(self.root,self.root)


if __name__=='__main__':unittest.main()
