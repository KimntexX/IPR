import json
from pathlib import Path
import tempfile
import unittest
from physical_continuation_study import summarize


class PairedContinuationTests(unittest.TestCase):
    def make_study(self,root):
        arms=[]
        for name,gain in (('current',2.),('normalized',3.)):
            folder=root/name/'seed71';folder.mkdir(parents=True)
            rows=[dict(target_index=i,iso=iso,observed_mse=.01,corrected_mse=.01*10**(-gain/10),psnr_gain_db=gain)
                  for i in range(18) for iso in (3200,6400,16000,51200)]
            result=dict(completed_steps=4096,status='completed_natural_physical_development_training',
                initial_model_fingerprint='same-parent',input_selection_trace_sha256='same-data',
                parent_control_reproduction_max_relative_error=0.,controls=[dict(step=4096,rows=rows)])
            (folder/'progress.json').write_text(json.dumps(result),encoding='utf-8')
            arms.append(dict(path=str(folder.parent)))
        return dict(arms=arms)

    def test_paired_report_preserves_per_source_and_iso_effects(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);study=self.make_study(root)
            result=summarize(root,study)
            self.assertEqual(len(result['rows']),72)
            for group in result['summary']:
                self.assertEqual(group['by_source_db'],[1.,1.])
                self.assertEqual(group['median_sources_db'],1.)

    def test_different_data_or_start_prevents_a_comparison(self):
        for field in ('initial_model_fingerprint','input_selection_trace_sha256'):
            with self.subTest(field=field),tempfile.TemporaryDirectory() as directory:
                root=Path(directory);study=self.make_study(root)
                path=root/'normalized/seed71/progress.json'
                data=json.loads(path.read_text());data[field]='different'
                path.write_text(json.dumps(data),encoding='utf-8')
                with self.assertRaises(ValueError):summarize(root,study)

    def test_incomplete_arm_cannot_produce_a_successful_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);study=self.make_study(root)
            path=root/'normalized/seed71/progress.json'
            data=json.loads(path.read_text());data['completed_steps']=2048
            path.write_text(json.dumps(data),encoding='utf-8')
            with self.assertRaises(ValueError):summarize(root,study)
