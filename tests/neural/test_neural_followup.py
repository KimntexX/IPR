import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

from run_neural_followup import next_action, execute, run


class NeuralFollowupTests(unittest.TestCase):
    def test_new_data_requires_all_known_gates(self):
        progress = dict(status='completed_known_regressions_passed', development_passed=True,
            regressions=[dict(name=n, passed=True) for n in ('classic_first', 'classic_second', 'fine')])
        self.assertEqual(next_action(progress), 'confirm')
        progress['regressions'][2]['passed'] = False
        with self.assertRaises(ValueError):
            next_action(progress)
        progress['status'] = 'completed_known_regressions_failed_reserved_unopened'
        self.assertEqual(next_action(progress), 'diagnose')
        self.assertEqual(next_action(dict(status='fine')), 'wait')
        self.assertEqual(next_action(dict(status='stopped_on_error')), 'stop')

    def test_known_failure_never_opens_new_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            base.mkdir()
            (root/'docs/validation/raw').mkdir(parents=True)
            qualification = base/'qualification.json'
            qualification.write_text(json.dumps(dict(status='completed_known_regressions_failed_reserved_unopened')))
            fine = base/'candidate-fine-v001'
            fine.mkdir()
            (fine/'result.json').write_text('{}')
            commands = []
            def fake_execute(command, log):
                commands.append(command)
                return 0
            with mock.patch('run_neural_followup.execute', side_effect=fake_execute):
                result = run(root, base/'output', qualification, base/'data.json', base/'training', 'candidate',
                    base/'previous-fine.json', base/'previous-training')
            self.assertEqual(result['status'], 'completed_known_failure_diagnosis_reserved_unopened')
            self.assertEqual(len(commands), 3)
            self.assertTrue(commands[0][1].endswith('compare_fine_candidates.py'))
            self.assertTrue(commands[1][1].endswith('trace_fine_attenuation.py'))
            self.assertTrue(commands[2][1].endswith('diagnose_multitone_training.py'))
            self.assertFalse(any('confirm_noisy_model.py' in str(c) or 'confirm_fine_noise.py' in str(c) for c in commands))

    def test_timeout_terminates_owned_windows_process_tree_and_reaps_child(self):
        child = mock.Mock(pid=12345)
        child.wait.side_effect = [subprocess.TimeoutExpired(['test'], 7200), 0]
        context = mock.MagicMock()
        context.__enter__.return_value = child
        with mock.patch('run_neural_followup.subprocess.Popen', return_value=context), mock.patch(
                'run_neural_followup.subprocess.run') as kill, mock.patch('run_neural_followup.os.name', 'nt'):
            with self.assertRaises(subprocess.TimeoutExpired):
                execute(['test'], io.StringIO())
        self.assertEqual(kill.call_args.args[0], ['taskkill', '/PID', '12345', '/T', '/F'])
        self.assertEqual(child.wait.call_count, 2)

    def test_repeatability_never_opens_reserved_even_when_all_known_gates_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            base=root/'build-patch_neural_restoration'
            base.mkdir()
            (root/'docs/validation/raw').mkdir(parents=True)
            qualification=base/'qualification.json'
            qualification.write_text(json.dumps(dict(status='completed_known_regressions_passed',development_passed=True,
                regressions=[dict(name=n,passed=True) for n in ('classic_first','classic_second','fine')])))
            data=base/'data.json'
            data.write_text(json.dumps(dict(qualification_scope='known_only')))
            with mock.patch('run_neural_followup.execute',side_effect=AssertionError('No reserved command allowed')),mock.patch(
                    'run_neural_followup.campaign_models',side_effect=AssertionError('No confirmation models allowed')):
                result=run(root,base/'output',qualification,data,base/'training','repeatability',base/'previous.json',base/'previous-training')
            self.assertEqual(result['status'],'completed_known_only_study_reserved_unopened')
