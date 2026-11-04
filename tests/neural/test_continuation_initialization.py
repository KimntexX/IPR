import json
from pathlib import Path
import tempfile
import unittest

import torch

from continuation_initialization import initialize_continuation
from diagnose_train_learnability import digest
from local_moment_unet import LocalMomentUNet
from noisy_moment_unet import NoisyMomentUNet, load_reconstruction


class ContinuationInitializationTests(unittest.TestCase):
    def test_learned_front_is_loaded_and_wrong_frozen_trunk_is_rejected_before_mutation(self):
        torch.manual_seed(750)
        clean = LocalMomentUNet(4).state_dict()
        learned = NoisyMomentUNet(4)
        load_reconstruction(learned, clean)
        with torch.no_grad():
            learned.noise_head.weight.fill_(.02)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root/'build-patch_neural_restoration/warm'
            folder.mkdir(parents=True)
            weights = folder/'weights.pt'
            torch.save(dict(state_dict=learned.state_dict(), plan_sha256='initial-plan'), weights)
            proof = dict(status='completed_synthetic_noise_pilot_not_photographic_qualification',
                seed=71, pilot_gates_passed=True, training_progress=dict(completed_optimizer_steps=2048),
                weights_sha256=digest(weights), plan_sha256='initial-plan')
            source = folder/'result.json'
            source.write_text(json.dumps(proof))
            ref = dict(seed=71, folder=str(folder), weights_sha256=digest(weights), result_sha256=digest(source))
            model = NoisyMomentUNet(4)
            initialize_continuation(root, model, clean, ref)
            self.assertTrue(torch.equal(model.noise_head.weight, learned.noise_head.weight))
            before = {n:v.clone() for n,v in model.state_dict().items()}
            wrong = {n:v.clone() for n,v in clean.items()}
            wrong['head.weight'].add_(1)
            with self.assertRaisesRegex(ValueError, 'frozen reconstruction'):
                initialize_continuation(root, model, wrong, ref)
            self.assertTrue(all(torch.equal(v, model.state_dict()[n]) for n,v in before.items()))
            source.write_text('{}')
            with self.assertRaises(ValueError):
                initialize_continuation(root, model, clean, ref)
