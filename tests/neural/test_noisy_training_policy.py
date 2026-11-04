import unittest
from pathlib import Path
import tempfile
from unittest.mock import patch, Mock

import torch

from bayer_canonical import predict
from joint_bayer_unet import sample_rgb
from local_moment_unet import LocalMomentUNet
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction
from noisy_training_policy import configure_trainable
from synthetic_bayer_data import PATTERNS
from run_noisy_moment_pilot import train
from run_synthetic_joint_pilot import tensor_hash


class NoisyTrainingPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_frozen_trunk_keeps_exact_zero_noise_but_front_receives_gradients(self):
        torch.manual_seed(701)
        witness = LocalMomentUNet(4).eval()
        torch.nn.init.uniform_(witness.head.weight, -.1, .1)
        model = NoisyMomentUNet(4)
        load_reconstruction(model, witness.state_dict())
        before = {n: p.detach().clone() for n, p in model.named_parameters()}
        counts = configure_trainable(model, 'cfa_front')
        self.assertGreater(counts['frozen_parameters'], counts['trainable_parameters'])
        optimizer = torch.optim.Adam(model.parameters(), lr=.001)
        target = torch.rand(1, 3, 32, 40)*1.4-.2
        labels = torch.tensor([PATTERNS[0]])
        observed = sample_rgb(target, labels)+torch.randn(1, 4, 16, 20)*.02
        prediction = predict(model, observed, torch.full_like(observed, .02), labels, False, True)
        prediction.sub(target).square().mean().backward()
        self.assertGreater(float(model.noise_head.weight.grad.abs().sum()), 0)
        self.assertTrue(all(p.grad is None for n, p in model.named_parameters() if not n.startswith('noise_')))
        optimizer.step()
        for n, p in model.named_parameters():
            if not n.startswith('noise_'):
                self.assertTrue(torch.equal(p, before[n]))
        self.assertTrue(any(not torch.equal(p, before[n]) for n, p in model.named_parameters() if n.startswith('noise_')))
        model.eval()
        with torch.inference_mode():
            for phase in PATTERNS:
                labels = torch.tensor([phase])
                packed = sample_rgb(target, labels)
                zero = torch.zeros_like(packed)
                self.assertTrue(torch.equal(predict(model, packed, zero, labels, False, True),
                    predict(witness, packed, zero, labels, True, True)))

    def test_joint_scope_and_unknown_policy(self):
        model = NoisyMomentUNet(4)
        configure_trainable(model, 'cfa_front')
        counts = configure_trainable(model, 'joint')
        self.assertEqual(counts['frozen_parameters'], 0)
        self.assertTrue(all(p.requires_grad for p in model.parameters()))
        with self.assertRaises(ValueError):
            configure_trainable(model, 'undefined')
        self.assertTrue(all(p.requires_grad for p in model.parameters()))

    def test_multiscale_context_and_standardized_inputs_receive_gradients(self):
        torch.manual_seed(709)
        model = NoisyMomentUNet(4)
        configure_trainable(model, 'cfa_front')
        self.assertTrue((model.noise_head.weight == 0).all())
        self.assertTrue((model.noise_head.bias == 0).all())
        with torch.no_grad():
            model.noise_head.weight.fill_(.02)
        target = torch.full((1, 3, 32, 40), .3)
        tags = torch.tensor([PATTERNS[0]])
        packed = sample_rgb(target, tags)+torch.randn(1, 4, 16, 20)*.02
        image = predict(model, packed, torch.full_like(packed, .02), tags, False, True)
        image.sub(target).square().mean().backward()
        entry = model.noise_features.enc1[0].weight
        before = entry.detach().clone()
        self.assertGreater(float(entry.grad[:, 36:].abs().sum()), 0)
        self.assertGreater(float(model.noise_features.middle[0].weight.grad.abs().sum()), 0)
        self.assertGreater(float(model.noise_features.statistics.weight.grad.abs().sum()), 0)
        self.assertTrue(torch.isfinite(entry.grad).all())
        optimizer = torch.optim.Adam(model.parameters(), lr=.001)
        optimizer.step()
        self.assertFalse(torch.equal(entry, before))

    def test_statistical_control_freezes_only_the_zero_projection(self):
        model = NoisyMomentUNet(4)
        configure_trainable(model, 'cfa_front', statistics_trainable=False)
        self.assertTrue(all(not p.requires_grad for p in model.noise_features.statistics.parameters()))
        self.assertTrue(model.noise_features.enc1[0].weight.requires_grad)
        self.assertEqual(int(torch.count_nonzero(model.noise_features.statistics.weight)), 0)
        configure_trainable(model, 'cfa_front', statistics_trainable=True)
        self.assertTrue(all(p.requires_grad for p in model.noise_features.statistics.parameters()))

    def test_actual_frozen_training_accepts_zero_noise_anchors(self):
        model = NoisyMomentUNet(4)
        configure_trainable(model, 'cfa_front')
        before = {n: p.detach().clone() for n, p in model.named_parameters() if not n.startswith('noise_')}
        schedule = [dict(ids=[0]*4, phases=[0, 1, 2, 3]) for _ in range(2)]
        source = dict(freeze=dict(train_slots=[dict(family='constant')],
            phase_schedule_sha256=tensor_hash(torch.tensor([[*s['ids'], *s['phases']] for s in schedule]))))
        config = dict(steps=2, accumulation=4, side=128, scale=2., learning_rate=.001, training_scope='cfa_front')
        def sample(step, position, index, phase):
            truth = torch.full((1, 3, 128, 128), .2)
            tags = torch.tensor([PATTERNS[phase]])
            return truth, sample_rgb(truth, tags), tags
        with tempfile.TemporaryDirectory() as directory, patch('run_noisy_moment_pilot.balanced_schedule', return_value=(schedule, None)), patch('run_noisy_moment_pilot.SampleStream') as stream:
            stream.return_value.sample.side_effect = sample
            losses, counts = train(model, source, config, Mock(), Path(directory)/'latest.pt',
                dict(config=config), {}, 1)
        self.assertEqual(len(losses), 2)
        self.assertEqual(sum(counts.values()), 8)
        for n, p in model.named_parameters():
            if n in before:
                self.assertTrue(torch.equal(p, before[n]))


if __name__ == '__main__':
    unittest.main()
