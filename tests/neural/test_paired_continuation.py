import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from prepare_warm_continuation import prefix_source
from run_paired_continuation import run


class PairedContinuationTests(unittest.TestCase):
    def test_prefix_source_preserves_original_and_historical_controls(self):
        source = dict(freeze=dict(train_slots=[dict(family='constant', seed=1, pattern=[0, 1, 3, 2])],
            config=dict(side=128, steps=2048), train_edge_events=[dict(step=2), dict(step=9)],
            evaluation_cases=['retained']))
        prefix = prefix_source(source, 4)
        self.assertEqual(prefix['freeze']['config']['steps'], 4)
        self.assertEqual(prefix['freeze']['train_edge_events'], [dict(step=2)])
        self.assertEqual(prefix['freeze']['evaluation_cases'], source['freeze']['evaluation_cases'])
        self.assertEqual(source['freeze']['config']['steps'], 2048)
        self.assertEqual(len(source['freeze']['train_edge_events']), 2)

    def test_both_arms_train_before_evaluation_and_missing_control_blocks_confirmation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root/'build-patch_neural_restoration'
            prepared = base/'prepared'
            (root/'docs/validation/raw').mkdir(parents=True)
            for arm in ('control', 'progressive'):
                (prepared/arm).mkdir(parents=True)
                (prepared/arm/'plan.json').write_text('{}')
            commands = []
            def fake(command, log):
                commands.append(command)
                if command[1].endswith(('run_noisy_campaign.py', 'run_stability_qualification.py')):
                    output = Path(command[command.index('--output')+1])
                    output.mkdir(parents=True)
                    name = 'campaign.json' if command[1].endswith('run_noisy_campaign.py') else 'progress.json'
                    value = dict(status='completed_both_seeds' if name == 'campaign.json' else 'stopped_on_development_failure')
                    (output/name).write_text(json.dumps(value))
                return 0
            with mock.patch('run_paired_continuation.execute', side_effect=fake):
                result = run(root, prepared, base/'output')
            self.assertEqual(result['status'], 'inconclusive_control_development_failed_reserved_unopened')
            self.assertTrue(all(c[1].endswith('run_noisy_campaign.py') for c in commands[:2]))
            self.assertTrue(all(c[1].endswith('run_stability_qualification.py') for c in commands[2:4]))
            self.assertFalse(any(c[1].endswith('run_neural_followup.py') for c in commands))
