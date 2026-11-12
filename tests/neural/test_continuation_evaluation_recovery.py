import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from campaign_artifacts import sha256
from recover_continuation_evaluation import run


class EvaluationRecoveryTests(unittest.TestCase):
    def test_recovery_only_evaluates_and_preserves_original_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            (root/'docs/validation/raw').mkdir(parents=True)
            hashes = {}
            for arm in ('control', 'progressive'):
                folder = base/'paired-warm-plan-v002'/arm
                folder.mkdir(parents=True)
                (folder/'plan.json').write_text('{}')
                hashes[arm] = sha256(folder/'plan.json')
                campaign = base/f'continuation-{arm}-campaign-v001'
                campaign.mkdir()
                (campaign/'campaign.json').write_text('{}')
            old = base/'paired-continuation-v001'
            old.mkdir()
            (old/'progress.json').write_text(json.dumps(dict(error='Failed execution: qualify_progressive', plans_sha256=hashes)))
            original_hash = sha256(old/'progress.json')
            control = base/'continuation-control-fine-v001'
            control.mkdir()
            (control/'result.json').write_text(json.dumps(dict(status='completed_fine_signal_regression_not_photographic_qualification')))
            commands = []
            def execute(command, log):
                commands.append(command)
                output = Path(command[command.index('--output')+1])
                output.mkdir(parents=True)
                (output/'progress.json').write_text(json.dumps(dict(status='completed_known_regressions_failed_reserved_unopened')))
                return 0
            with mock.patch('recover_continuation_evaluation.campaign_models', return_value=[]), mock.patch(
                    'recover_continuation_evaluation.capacity', return_value={}), mock.patch(
                    'recover_continuation_evaluation.execute', side_effect=execute):
                run(root, base/'recovery')
            self.assertEqual(len(commands), 2)
            self.assertTrue(commands[0][1].endswith('run_stability_qualification.py'))
            self.assertTrue(commands[1][1].endswith('run_neural_followup.py'))
            self.assertEqual(sha256(old/'progress.json'), original_hash)
