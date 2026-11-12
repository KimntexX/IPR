import copy
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import torch

from joint_bayer_unet import sample_rgb
from memory_budget import BudgetExceeded
from noisy_moment_unet import NoisyMomentUNet
from run_noisy_moment_pilot import train
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from training_resume import save_checkpoint, restore_checkpoint


class Guard:
    def __init__(self, stop_at=None):
        self.calls, self.stop_at = 0, stop_at

    def check(self):
        self.calls += 1
        if self.calls == self.stop_at:
            raise BudgetExceeded('test interruption inside accumulation')


class TrainingResumeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)
        torch.use_deterministic_algorithms(True)

    def context(self):
        return dict(plan_sha256='fixed-plan', seed=71, parent_weights_sha256='parent',
            source_result_sha256='source', config=dict(steps=4, accumulation=4,
                side=128, scale=2., learning_rate=.001))

    def assert_tree_equal(self, a, b):
        if isinstance(a, torch.Tensor):
            self.assertTrue(torch.equal(a, b))
        elif isinstance(a, dict):
            self.assertEqual(a.keys(), b.keys())
            for key in a:
                self.assert_tree_equal(a[key], b[key])
        elif isinstance(a, (list, tuple)):
            self.assertEqual(len(a), len(b))
            for x, y in zip(a, b):
                self.assert_tree_equal(x, y)
        else:
            self.assertEqual(a, b)

    def test_real_training_resume_matches_continuous_after_partial_microbatch(self):
        context = self.context()
        schedule = [dict(ids=[0]*4, phases=[0, 1, 2, 3]) for _ in range(4)]
        source = dict(freeze=dict(train_slots=[dict(family='constant')],
            phase_schedule_sha256=tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule]))))
        axis = torch.linspace(-1, 1, 128)[None, None, None, :]
        template = (.2+.05*axis).expand(1, 3, 128, 128).clone()
        def sample(step, position, index, phase):
            # A global RNG draw verifies actual restoration, not just reseeding noise.
            target = template+torch.rand(())*.01
            tags = torch.tensor([PATTERNS[phase]])
            return target, sample_rgb(target, tags), tags
        def model():
            torch.manual_seed(701)
            return NoisyMomentUNet(4)
        with tempfile.TemporaryDirectory() as directory, patch('run_noisy_moment_pilot.balanced_schedule', return_value=(schedule, None)), patch('run_noisy_moment_pilot.SampleStream') as stream:
            root = Path(directory)
            stream.return_value.sample.side_effect = sample
            continuous = model()
            torch.manual_seed(902)
            expected = train(continuous, source, context['config'], Guard(), root/'continuous.pt', context,
                dict(completed_optimizer_steps=0), 2)
            interrupted = model()
            torch.manual_seed(902)
            progress = dict(completed_optimizer_steps=0)
            with self.assertRaises(BudgetExceeded):
                train(interrupted, source, context['config'], Guard(13), root/'interrupted.pt', context, progress, 2)
            self.assertEqual(progress['completed_optimizer_steps'], 2)
            self.assertEqual(progress['checkpoint']['completed_step'], 2)
            resumed = model()
            actual = train(resumed, source, context['config'], Guard(), root/'resumed.pt', context,
                dict(completed_optimizer_steps=0), 2, root/'interrupted.pt')
            self.assertEqual(expected, actual)
            self.assert_tree_equal(continuous.state_dict(), resumed.state_dict())
            self.assert_tree_equal(torch.load(root/'continuous.pt', weights_only=True), torch.load(root/'resumed.pt', weights_only=True))

    def test_atomic_failure_keeps_prior_checkpoint_and_removes_temporary_file(self):
        model = NoisyMomentUNet(4)
        optimizer = torch.optim.Adam(model.parameters(), lr=.001)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root/'latest.pt'
            first = save_checkpoint(path, model, optimizer, self.context(), 0, [], {})
            original = path.read_bytes()
            for operation in ('training_resume.torch.save', 'training_resume.os.replace'):
                with patch(operation, side_effect=OSError('injected write failure')), self.assertRaises(OSError):
                    save_checkpoint(path, model, optimizer, self.context(), 0, [], {})
                self.assertEqual(path.read_bytes(), original)
                self.assertEqual([p.name for p in root.iterdir()], ['latest.pt'])
                restored = restore_checkpoint(path, model, optimizer, self.context())
                self.assertEqual(restored[-1]['sha256'], first['sha256'])

    def test_refuses_wrong_context_and_inconsistent_counters_before_mutation(self):
        model = NoisyMomentUNet(4)
        optimizer = torch.optim.Adam(model.parameters(), lr=.001)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'latest.pt'
            save_checkpoint(path, model, optimizer, self.context(), 0, [], {})
            before = copy.deepcopy(model.state_dict())
            rng = torch.get_rng_state().clone()
            wrong = dict(self.context(), seed=73)
            with self.assertRaises(ValueError):
                restore_checkpoint(path, model, optimizer, wrong)
            self.assert_tree_equal(before, model.state_dict())
            self.assertTrue(torch.equal(rng, torch.get_rng_state()))
            with self.assertRaises(ValueError):
                save_checkpoint(path, model, optimizer, self.context(), 1, [1.], {'one_instead_of_four': 1})
            with self.assertRaises(ValueError):
                train(model, {}, self.context()['config'], Guard(), path, self.context(), {}, 0)


if __name__ == '__main__':
    unittest.main()
