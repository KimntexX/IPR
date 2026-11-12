import unittest
import torch
import json
from pathlib import Path
import tempfile
from unittest.mock import patch

from bayer_canonical import predict
from local_moment_unet import LocalMomentUNet
from prepare_closed_inference import prepare_inference
from joint_bayer_unet import sample_rgb
from synthetic_bayer_data import PATTERNS
from confirm_local_route import prepare


class LocalConfirmationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_preparation_includes_both_heads_and_is_bit_exact(self):
        torch.manual_seed(121)
        model = LocalMomentUNet(4).eval()
        for head in (model.head, model.local_head):
            torch.nn.init.uniform_(head.weight, -.1, .1)
            torch.nn.init.uniform_(head.bias, -.1, .1)
        source_state = {n: v.clone() for n, v in model.state_dict().items()}
        rng = torch.get_rng_state().clone()
        prepared = prepare_inference(model)
        self.assertTrue(torch.equal(rng, torch.get_rng_state()))
        truth = torch.rand(1, 3, 32, 40)*1.7-.3
        with torch.inference_mode():
            for p in PATTERNS:
                tags = torch.tensor([p])
                packed = sample_rgb(truth, tags)
                args = (packed/2, torch.zeros_like(packed), tags, True, True)
                self.assertTrue(torch.equal(predict(model, *args), predict(prepared, *args)))
        self.assertTrue(all(torch.equal(v, model.state_dict()[n]) for n, v in source_state.items()))

    def test_new_plan_has_distinct_seed_blocks_and_rejects_training_overlap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root/'build-patch_neural_restoration'/'source'
            source.mkdir(parents=True)
            proof = dict(status='completed_diverse_contour_reconstruction_not_photographic_qualification',
                freeze=dict(train_slots=[], train_edge_events=[]))
            (source/'result.json').write_text(json.dumps(proof), encoding='utf-8')
            output = root/'build-patch_neural_restoration'/'plan'
            with patch('confirm_local_route.MemoryGuard') as guard:
                guard.return_value.stats.return_value = dict(test_only=True)
                prepare(root, source, output)
            plan = json.loads((output/'plan.json').read_text(encoding='utf-8'))
            self.assertEqual({r['seed'] for r in plan['edges']}, set(range(2000128, 2000160)))
            self.assertEqual(len(plan['mixed']), 20)
            self.assertEqual(len({r['target_sha256'] for r in plan['edges']+plan['mixed']}), 52)
            self.assertEqual({r['normal_bin'] for r in plan['edges']}, set(range(32)))
            proof['freeze']['train_edge_events'] = [dict(target_sha256=plan['edges'][0]['target_sha256'])]
            (source/'result.json').write_text(json.dumps(proof), encoding='utf-8')
            with patch('confirm_local_route.MemoryGuard'), self.assertRaises(ValueError):
                prepare(root, source, root/'build-patch_neural_restoration'/'overlap')


if __name__ == '__main__':
    unittest.main()
