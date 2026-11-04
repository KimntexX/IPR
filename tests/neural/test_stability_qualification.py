import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from run_stability_qualification import run


class StabilityQualificationTests(unittest.TestCase):
    def test_fine_failure_skips_classic_regressions_without_claiming_their_result(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            campaign = base/'training'
            campaign.mkdir(parents=True)
            (root/'docs/validation/raw').mkdir(parents=True)
            (campaign/'campaign.json').write_text(json.dumps(dict(status='completed_both_seeds')))
            for seed in (71, 73):
                folder = campaign/f'seed{seed}-attempt01'
                folder.mkdir()
                (folder/'weights.pt').write_bytes(b'test')
            commands = []
            def execute(command, **kwargs):
                commands.append(command)
                if '--prepare' not in command and command[1].endswith('run_edge_protected_probe.py'):
                    folder = base/'candidate-protected-v001'
                    folder.mkdir()
                    (folder/'result.json').write_text(json.dumps(dict(status='completed_probe', pilot_gates_passed=True)))
                if command[1].endswith('confirm_fine_noise.py'):
                    folder = base/'candidate-fine-v001'
                    folder.mkdir()
                    (folder/'result.json').write_text(json.dumps(dict(status='completed_fine', pilot_gates_passed=False)))
                return mock.Mock(returncode=0)
            with mock.patch('run_stability_qualification.subprocess.run', side_effect=execute):
                result = run(root, base/'output', campaign, base/'plan.json', 'candidate')
            self.assertEqual(result['status'], 'completed_known_regressions_failed_reserved_unopened')
            self.assertEqual([r['name'] for r in result['regressions']], ['fine'])
            self.assertFalse(any(c[1].endswith('confirm_noisy_model.py') for c in commands))

    def test_failed_development_stops_before_regressions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            campaign = base/'noisy-stability-campaign-v001'
            campaign.mkdir(parents=True)
            (root/'docs/validation/raw').mkdir(parents=True)
            (campaign/'campaign.json').write_text(json.dumps(dict(status='completed_both_seeds')))
            for seed in (71, 73):
                folder = campaign/f'seed{seed}-attempt01'
                folder.mkdir()
                (folder/'weights.pt').write_bytes(b'fake-test-checkpoint')
            calls = []
            def execute(command, **kwargs):
                calls.append(command)
                if '--prepare' not in command:
                    destination = base/'stability-protected-v001'
                    destination.mkdir()
                    (destination/'result.json').write_text(json.dumps(dict(
                        status='completed_fixed_edge_protection_probe_not_photographic_qualification',
                        pilot_gates_passed=False)))
                return mock.Mock(returncode=0)
            with mock.patch('run_stability_qualification.subprocess.run', side_effect=execute):
                result = run(root, base/'test-output', campaign, base/'plan.json', 'stability')
            self.assertEqual(result['status'], 'stopped_on_development_failure')
            self.assertEqual(len(calls), 2)
            self.assertFalse(any('prepare_neural_regression.py' in str(c) for c in calls))
            self.assertEqual(len(list((root/'docs/validation/raw').glob('*.json'))), 1)

    def test_incomplete_training_is_preserved_as_error_without_starting_jobs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            campaign = base/'noisy-stability-campaign-v001'
            campaign.mkdir(parents=True)
            (root/'docs/validation/raw').mkdir(parents=True)
            (campaign/'campaign.json').write_text(json.dumps(dict(status='stopped_on_calculation_error')))
            with mock.patch('run_stability_qualification.subprocess.run') as execute:
                with self.assertRaisesRegex(RuntimeError, 'Training campaign incomplete'):
                    run(root, base/'test-output', campaign, base/'plan.json', 'stability')
            execute.assert_not_called()
            state = json.loads((base/'test-output/progress.json').read_text())
            self.assertEqual(state['status'], 'stopped_on_error')
