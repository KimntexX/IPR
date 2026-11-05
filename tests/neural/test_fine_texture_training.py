import unittest

import torch

from fine_texture_training import texture


class FineTextureTrainingTests(unittest.TestCase):
    def test_deterministic_local_rng_and_declared_ranges(self):
        state = torch.get_rng_state().clone()
        for family in ('weak_achromatic_wave', 'chromatic_wave'):
            first, metadata = texture(family, 10, 128)
            second, repeated = texture(family, 10, 128)
            self.assertTrue(torch.equal(first, second))
            self.assertEqual(metadata, repeated)
            self.assertEqual(dict(phase=2, **metadata)['phase'], 2)
            self.assertEqual(first.shape, (1, 3, 128, 128))
            self.assertTrue(torch.isfinite(first).all())
            self.assertLessEqual(float(first.max()), metadata['dc']+metadata['amplitude']+1e-6)
            self.assertGreaterEqual(float(first.min()), metadata['dc']-metadata['amplitude']-1e-6)
            self.assertTrue(2 <= metadata['frequency'] <= 28)
            if family == 'weak_achromatic_wave':
                self.assertTrue(torch.equal(first[:, 0], first[:, 2]))
            else:
                self.assertFalse(torch.equal(first[:, 0], first[:, 2]))
        self.assertTrue(torch.equal(state, torch.get_rng_state()))

    def test_new_serial_changes_content_and_rejects_invalid_request(self):
        a, _ = texture('chromatic_wave', 10, 128)
        b, _ = texture('chromatic_wave', 11, 128)
        self.assertFalse(torch.equal(a, b))
        for args in (('edge', 1, 128), ('chromatic_wave', -1, 128), ('chromatic_wave', True, 128),
                ('chromatic_wave', 1, 192)):
            with self.assertRaises(ValueError):
                texture(*args)

    def test_actual_augmented_training_resume_matches_continuous(self):
        from pathlib import Path
        import tempfile
        from unittest.mock import patch
        from memory_budget import BudgetExceeded
        from noisy_moment_unet import NoisyMomentUNet
        from noisy_training_policy import configure_trainable
        from run_noisy_moment_pilot import train
        from run_synthetic_joint_pilot import tensor_hash
        torch.set_num_threads(2)
        schedule = [dict(ids=[0]*4, phases=[0, 1, 2, 3]) for _ in range(4)]
        family = 'chromatic_wave'
        source = dict(freeze=dict(train_slots=[dict(family=family)], train_edge_events=[], config=dict(side=128),
            phase_schedule_sha256=tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule]))))
        events = [dict(serial=s, texture_serial=4000000+s, family=family, phase=s%4,
            target_sha256=tensor_hash(texture(family, 4000000+s, 128)[0])) for s in range(16)]
        config = dict(steps=4, accumulation=4, side=128, scale=2., learning_rate=.001)
        context = dict(plan_sha256='fixed', config=config, seed=71,
            parent_weights_sha256='parent', source_result_sha256='source')
        class Guard:
            def __init__(self, stop=None):
                self.calls, self.stop = 0, stop
            def check(self):
                self.calls += 1
                if self.calls == self.stop:
                    raise BudgetExceeded('test')
        def model():
            torch.manual_seed(701)
            m = NoisyMomentUNet(4)
            configure_trainable(m, 'cfa_front')
            return m
        with tempfile.TemporaryDirectory() as directory, patch('run_noisy_moment_pilot.balanced_schedule', return_value=(schedule, None)):
            folder = Path(directory)
            continuous = model()
            expected = train(continuous, source, config, Guard(), folder/'full.pt', context, {}, 2,
                texture_events=events)
            interrupted, progress = model(), {}
            with self.assertRaises(BudgetExceeded):
                train(interrupted, source, config, Guard(13), folder/'partial.pt', context, progress, 2,
                    texture_events=events)
            self.assertEqual(progress['completed_optimizer_steps'], 2)
            resumed = model()
            actual = train(resumed, source, config, Guard(), folder/'resumed.pt', context, {}, 2,
                resume=folder/'partial.pt', texture_events=events)
            self.assertEqual(actual, expected)
            for name, value in continuous.state_dict().items():
                self.assertTrue(torch.equal(value, resumed.state_dict()[name]), name)
            with self.assertRaisesRegex(ValueError, 'coverage'):
                train(model(), source, config, Guard(), folder/'invalid.pt', context, {}, 2,
                    texture_events=events[:-1])


if __name__ == '__main__':
    unittest.main()
