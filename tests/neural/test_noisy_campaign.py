from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from run_noisy_campaign import capacity, resume_from, run, sha256


class NoisyCampaignTests(unittest.TestCase):
    def test_capacity_requires_headroom_and_stops_at_latest_start(self):
        until = datetime.now(timezone.utc)+timedelta(minutes=10)
        samples = [dict(system_used_fraction=f) for f in (.91, .89, .88)]
        with patch('run_noisy_campaign.snapshot', side_effect=samples) as status, patch('run_noisy_campaign.time.sleep') as sleep:
            self.assertEqual(capacity(until, .90, 15), samples[-1])
            self.assertEqual(status.call_count, 3)
            self.assertEqual(sleep.call_count, 2)
        with patch('run_noisy_campaign.snapshot') as status:
            self.assertIsNone(capacity(datetime.now(timezone.utc)-timedelta(seconds=1), .90, 15))
            status.assert_not_called()

    def test_resume_refuses_foreign_path_or_modified_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            campaign = root/'campaign'
            campaign.mkdir()
            path = campaign/'training-latest.pt'
            path.write_bytes(b'checkpoint')
            result = dict(training_progress=dict(checkpoint=dict(path=str(path), sha256=sha256(path))))
            self.assertEqual(resume_from(result, campaign), path.resolve())
            with self.assertRaises(ValueError):
                resume_from(result, campaign/'other')
            path.write_bytes(b'changed')
            with self.assertRaises(ValueError):
                resume_from(result, campaign)

    def fixture(self, root):
        (root/'.venv-neural/Scripts').mkdir(parents=True)
        (root/'.venv-neural/Scripts/python.exe').write_bytes(b'not executed in unit test')
        (root/'docs/validation/raw').mkdir(parents=True)
        base = root/'build-patch_neural_restoration'
        base.mkdir()
        plan = base/'plan.json'
        plan.write_text(json.dumps(dict(schema_version=3, checkpoint_policy=dict(interval_steps=64),
            memory_policy=dict(system_fraction=.92))), encoding='utf-8')
        return plan, base/'campaign'

    def fake_attempt(self, command, returncode, status, qualified=None):
        output = Path(command[command.index('--output')+1])
        seed = int(command[command.index('--seed')+1])
        output.mkdir()
        checkpoint = output/'training-latest.pt'
        checkpoint.write_bytes(b'owned checkpoint')
        result = dict(status=status, campaign_id=f'test-{seed}-{output.name}', pilot_gates_passed=qualified,
            training_progress=dict(checkpoint=dict(path=str(checkpoint), sha256=sha256(checkpoint), completed_step=64)))
        (output/'result.json').write_text(json.dumps(result), encoding='utf-8')
        return SimpleNamespace(returncode=returncode)

    def test_memory_retry_resumes_and_quality_failure_still_runs_other_seed(self):
        commands = []
        def child(command, **kwargs):
            commands.append(command)
            if len(commands) == 1:
                return self.fake_attempt(command, 1, 'aborted_for_memory_budget')
            return self.fake_attempt(command, 0, 'completed_synthetic_noise_pilot_not_photographic_qualification', len(commands) == 3)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan, output = self.fixture(root)
            with patch('run_noisy_campaign.capacity', return_value=dict(system_used_fraction=.5)), patch('run_noisy_campaign.subprocess.run', side_effect=child):
                result = run(root, plan, output, datetime.now(timezone.utc)+timedelta(hours=1))
            self.assertEqual([int(c[c.index('--seed')+1]) for c in commands], [71, 71, 73])
            self.assertIn('--resume', commands[1])
            self.assertEqual(Path(commands[1][commands[1].index('--resume')+1]), output/'seed71-attempt01/training-latest.pt')
            self.assertNotIn('--resume', commands[2])
            self.assertEqual(result['status'], 'completed_both_seeds')
            self.assertFalse(result['pilot_gates_passed'])
            self.assertTrue((output/'campaign.json').is_file())

    def test_nonmemory_error_is_not_retried(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan, output = self.fixture(root)
            with patch('run_noisy_campaign.capacity', return_value=dict(system_used_fraction=.5)), patch('run_noisy_campaign.subprocess.run', side_effect=lambda command, **kw: self.fake_attempt(command, 1, 'incomplete')) as child:
                result = run(root, plan, output, datetime.now(timezone.utc)+timedelta(hours=1))
            self.assertEqual(child.call_count, 1)
            self.assertEqual(result['status'], 'stopped_on_calculation_error')

    def test_retry_limit_and_timeout_end_campaign_without_more_children(self):
        import subprocess
        for timeout in (False, True):
            with self.subTest(timeout=timeout), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                plan, output = self.fixture(root)
                def child(command, **kwargs):
                    if timeout:
                        raise subprocess.TimeoutExpired(command, 1800)
                    return self.fake_attempt(command, 1, 'aborted_for_memory_budget')
                with patch('run_noisy_campaign.capacity', return_value=dict(system_used_fraction=.5)), patch('run_noisy_campaign.subprocess.run', side_effect=child) as process:
                    result = run(root, plan, output, datetime.now(timezone.utc)+timedelta(hours=1), max_attempts=1)
                self.assertEqual(process.call_count, 1)
                self.assertEqual(result['status'], 'stopped_on_attempt_timeout' if timeout else 'memory_retry_limit_reached')
                log = output/'seed71-attempt01.log'
                log.rename(output/'closed.log')


if __name__ == '__main__':
    unittest.main()
