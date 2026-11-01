"""No model calls: fake CLI integration and controller failure paths."""
import importlib.util
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock

SOURCE = Path(__file__).resolve().parents[1]/'scripts/run_test_then_analyse.py'
SPEC = importlib.util.spec_from_file_location('completion_bridge', SOURCE)
bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bridge)


class CompletionBridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'.git').mkdir()
        self.output = self.root/'jobs'/'one'

    def args(self, *options, code='print("test finished")'):
        return bridge.parser().parse_args(['--workspace', str(self.root), '--output-dir', str(self.output),
                                          *options, '--', sys.executable, '-c', code])

    def state(self):
        return json.loads((self.output/'completion.json').read_text(encoding='utf-8'))

    def test_default_never_calls_model(self):
        with patch.object(bridge, 'codex_command', side_effect=AssertionError('Must not call model')):
            self.assertEqual(bridge.run(self.args()), 0)
        self.assertEqual(self.state()['phase'], 'finished_without_model_call')
        self.assertIn('test finished', (self.output/'test.stdout.log').read_text())

    def test_failure_preserved_without_model(self):
        self.assertEqual(bridge.run(self.args(code='import sys; print("bad",file=sys.stderr); sys.exit(7)')), 1)
        self.assertEqual(self.state()['test']['returncode'], 7)
        self.assertIn('bad', (self.output/'test.stderr.log').read_text())

    def test_timeout_is_bounded(self):
        self.assertEqual(bridge.run(self.args('--test-timeout', '.2', code='import time; time.sleep(30)')), 2)
        self.assertTrue(self.state()['test']['timed_out'])

    def test_output_not_reused(self):
        bridge.run(self.args())
        original = (self.output/'completion.json').read_bytes()
        with self.assertRaises(FileExistsError):
            bridge.run(self.args())
        self.assertEqual((self.output/'completion.json').read_bytes(), original)

    def test_paths_and_timeouts_rejected_before_execution(self):
        for option in ('0', '-1', 'nan', 'inf'):
            with self.assertRaises(ValueError):
                bridge.run(self.args('--test-timeout', option))
        args = self.args(); args.output_dir = self.root
        with self.assertRaises(ValueError):
            bridge.run(args)
        args.output_dir = self.root/'.git'/'job'
        with self.assertRaises(ValueError):
            bridge.run(args)

    def test_fake_analysis_called_once(self):
        fake = ('import pathlib,sys; prompt=sys.stdin.read(); '
                'assert "Aucun commit" in prompt; '
                'pathlib.Path(sys.argv[1]).write_text("Analyse simulée",encoding="utf-8"); '
                'print("fake event")')
        with patch.object(bridge, 'codex_command', return_value=[sys.executable, '-c', fake, str(self.output/'analysis.md')]) as command:
            self.assertEqual(bridge.run(self.args('--analyse', '--codex', sys.executable)), 0)
            self.assertEqual(command.call_count, 1)
        self.assertEqual(self.state()['phase'], 'analysis_complete')

    def test_test_failure_can_be_analysed_without_masking_failure(self):
        fake = 'import pathlib,sys; sys.stdin.read(); pathlib.Path(sys.argv[1]).write_text("Failure explained")'
        with patch.object(bridge, 'codex_command', return_value=[sys.executable, '-c', fake, str(self.output/'analysis.md')]):
            self.assertEqual(bridge.run(self.args('--analyse', '--codex', sys.executable, code='raise SystemExit(9)')), 1)
        self.assertEqual(self.state()['test']['returncode'], 9)

    def test_analysis_failure_not_retried(self):
        with patch.object(bridge, 'codex_command', return_value=[sys.executable, '-c', 'raise SystemExit(6)']) as command:
            self.assertEqual(bridge.run(self.args('--analyse', '--codex', sys.executable)), 3)
            self.assertEqual(command.call_count, 1)
        self.assertEqual(self.state()['phase'], 'analysis_failed_no_retry')

    def test_analysis_timeout_not_retried(self):
        with patch.object(bridge, 'codex_command', return_value=[sys.executable, '-c', 'import time; time.sleep(30)']) as command:
            self.assertEqual(bridge.run(self.args('--analyse', '--codex', sys.executable, '--analysis-timeout', '.2')), 3)
            self.assertEqual(command.call_count, 1)
        self.assertTrue(self.state()['analysis']['timed_out'])

    def test_real_command_has_explicit_safety_flags(self):
        command = bridge.codex_command('codex.exe', self.root, self.output/'analysis.md')
        self.assertEqual(command[:4], ['codex.exe', '--ask-for-approval', 'never', 'exec'])
        self.assertIn('read-only', command)
        self.assertIn('--ignore-user-config', command)
        self.assertIn('--ephemeral', command)
        self.assertNotIn('resume', command)
        self.assertNotIn('--last', command)

    def test_windows_tree_kill_timeout_falls_back_without_unbounded_wait(self):
        process = Mock(pid=123)
        process.poll.return_value = None
        with patch.object(bridge.os, 'name', 'nt'), patch.object(bridge.subprocess, 'run',
                side_effect=subprocess.TimeoutExpired('taskkill', 20)):
            bridge.stop_owned_process(process)
        process.kill.assert_called_once()
        process.wait.assert_called_once_with(timeout=20)

    def test_spawn_error_preserved(self):
        args = self.args(); args.command = ['--', str(self.root/'missing-program.exe')]
        with self.assertRaises(OSError):
            bridge.run(args)
        self.assertEqual(self.state()['phase'], 'controller_failed')


if __name__ == '__main__':
    unittest.main()
