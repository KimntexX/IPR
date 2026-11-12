from pathlib import Path
import tempfile
import unittest
from unittest import mock

import torch

from fine_texture_training import POLICY, texture
from memory_budget import BudgetExceeded
from noisy_moment_unet import NoisyMomentUNet
from noisy_training_policy import configure_trainable
from run_balanced_moment_pilot import balanced_schedule
from run_diverse_moment_pilot import edge_recipe, freeze_edge
from run_noisy_moment_pilot import train
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import FAMILIES, PATTERNS


class Guard:
    def __init__(self, stop=None):
        self.calls, self.stop = 0, stop
    def check(self):
        self.calls += 1
        if self.calls == self.stop:
            raise BudgetExceeded('test context interruption')


class ContextTrainingTests(unittest.TestCase):
    def test_real_context_training_recovery_and_frozen_reconstruction(self):
        torch.set_num_threads(2)
        torch.use_deterministic_algorithms(True)
        slots = [dict(family=f, seed=710+i, pattern=list(PATTERNS[0])) for i, f in enumerate(FAMILIES)]
        schedule, _ = balanced_schedule(slots, 4, 4, 128)
        edges, textures = [], []
        for step, entry in enumerate(schedule, 1):
            for pos, (index, phase) in enumerate(zip(entry['ids'], entry['phases'])):
                family = slots[index]['family']
                serial = 4*(step-1)+pos
                if family in POLICY['families']:
                    textures.append(dict(serial=serial, family=family, phase=phase,
                        target_sha256=tensor_hash(texture(family, serial, 128)[0])))
                elif family == 'edge':
                    edges.append(dict(freeze_edge(edge_recipe(serial, 'train'), 128),
                        step=step, batch_position=pos))
        source = dict(freeze=dict(train_slots=slots, train_edge_events=edges, config=dict(side=128),
            phase_schedule_sha256=tensor_hash(torch.tensor([[*e['ids'], *e['phases']] for e in schedule]))))
        config = dict(steps=4, side=256, source_side=128, loss_border=112,
            scale=2., accumulation=4, learning_rate=.001,
            structure_schedule=dict(start=0., end=1., ramp_steps=3))
        context = dict(plan_sha256='fixed-context-plan', config=config, seed=71,
            parent_weights_sha256='parent', source_result_sha256='source')
        def model():
            torch.manual_seed(718)
            m = NoisyMomentUNet(4)
            configure_trainable(m, 'cfa_front')
            return m
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            continuous = model()
            frozen = {k: v.detach().clone() for k, v in continuous.named_parameters() if not k.startswith('noise_')}
            step = torch.optim.Adam.step
            checked_norms = []
            def bounded_step(optimizer, *args, **kwargs):
                gradients = [p.grad for group in optimizer.param_groups for p in group['params'] if p.grad is not None]
                norm = float(torch.stack([g.square().sum() for g in gradients]).sum().sqrt())
                self.assertLessEqual(norm, 1.00001)
                checked_norms.append(norm)
                return step(optimizer, *args, **kwargs)
            with mock.patch.object(torch.optim.Adam, 'step', bounded_step):
                expected = train(continuous, source, config, Guard(), folder/'complete.pt', context, {}, 2,
                    texture_events=textures)
            self.assertEqual(len(checked_norms), 4)
            interrupted, progress = model(), {}
            with self.assertRaises(BudgetExceeded):
                train(interrupted, source, config, Guard(13), folder/'partial.pt', context, progress, 2,
                    texture_events=textures)
            self.assertEqual(progress['completed_optimizer_steps'], 2)
            resumed = model()
            actual = train(resumed, source, config, Guard(), folder/'resumed.pt', context, {}, 2,
                resume=folder/'partial.pt', texture_events=textures)
            self.assertEqual(expected, actual)
            self.assertEqual(sum(actual[1].values()), 16)
            for name, value in continuous.state_dict().items():
                self.assertTrue(torch.equal(value, resumed.state_dict()[name]), name)
            for name, value in continuous.named_parameters():
                if name in frozen:
                    self.assertTrue(torch.equal(value, frozen[name]), name)
