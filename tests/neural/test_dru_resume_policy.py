import json
from pathlib import Path
import tempfile
import unittest
from campaign_artifacts import sha256
from dru_resume_policy import action


class ResumePolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)/'seed73';self.folder.mkdir()

    def write(self,state):
        (self.folder/'result.json').write_text(json.dumps(state),encoding='utf-8')

    def interrupted(self):
        path=self.folder/'resume.pt';path.write_bytes(b'owned durable state fixture')
        (self.folder/'resume.json').write_text(json.dumps(dict(path=str(path),step=160,sha256=sha256(path))),encoding='utf-8')
        self.write(dict(status='stopped_on_error',completed_steps=187,plan_sha256='unchanged'))

    def test_interrupted_run_uses_durable_checkpoint_and_refuses_wrong_plan(self):
        self.interrupted();self.assertEqual(action(self.folder,'unchanged',256),'resume')
        with self.assertRaises(ValueError):action(self.folder,'different',256)

    def test_tampered_checkpoint_and_active_worker_refused(self):
        self.interrupted();(self.folder/'resume.pt').write_bytes(b'altered')
        with self.assertRaises(ValueError):action(self.folder,'unchanged',256)
        self.write(dict(status='started',plan_sha256='unchanged'))
        with self.assertRaises(ValueError):action(self.folder,'unchanged',256)

    def test_complete_result_reused_only_after_artifact_verification(self):
        path=self.folder/'weights.pt';path.write_bytes(b'complete export fixture')
        self.write(dict(status='completed_dru_bootstrap_not_photographic_qualification',completed_steps=256,
            losses=[0.]*256,plan_sha256='unchanged',milestones=[dict(path=str(path),step=256,sha256=sha256(path))],controls=[]))
        self.assertEqual(action(self.folder,'unchanged',256),'reuse')
        path.write_bytes(b'altered')
        with self.assertRaises(ValueError):action(self.folder,'unchanged',256)

    def test_new_folder_selected_without_writing_anything(self):
        absent=self.folder/'absent';self.assertEqual(action(absent,'unchanged',256),'new');self.assertFalse(absent.exists())


if __name__=='__main__':unittest.main()
