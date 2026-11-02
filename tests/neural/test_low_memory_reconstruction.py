import unittest
import torch

from bayer_canonical import predict
from closed_moment_unet import build_model
from run_memory_bounded_reconstruction import SampleStream
from joint_bayer_unet import sample_rgb
from synthetic_bayer_data import PATTERNS


class LowMemoryReconstructionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_microbatch_gradient_matches_effective_batch_without_retaining_graphs(self):
        torch.manual_seed(111)
        model = build_model('equivariant', 4)
        torch.nn.init.uniform_(model.head.weight, -.05, .05)
        truth = torch.rand(4, 3, 32, 32)
        tags = torch.tensor(PATTERNS)
        packed = sample_rgb(truth, tags)
        output = predict(model, packed/2, torch.zeros_like(packed), tags, True, True)
        loss = (output-truth/2).square().mean()
        loss.backward()
        gradients = {n: p.grad.clone() for n, p in model.named_parameters()}
        model.zero_grad(set_to_none=True)
        for i in range(4):
            output = predict(model, packed[i:i+1]/2, torch.zeros_like(packed[i:i+1]), tags[i:i+1], True, True)
            ((output-truth[i:i+1]/2).square().mean()/4).backward()
        for name, p in model.named_parameters():
            torch.testing.assert_close(p.grad, gradients[name], rtol=2e-4, atol=2e-7)

    def test_fixed_signal_cache_is_bounded_and_returns_single_images(self):
        cases = [dict(family='constant', seed=7300+i) for i in range(10)]
        proof = dict(freeze=dict(train_slots=cases, train_edge_events=[], config=dict(side=32)))
        stream = SampleStream(proof)
        for i in range(10):
            truth, packed, tags = stream.sample(1, 0, i, i % 4)
            self.assertEqual(truth.shape, (1, 3, 32, 32))
            self.assertTrue(torch.equal(sample_rgb(truth, tags), packed))
            self.assertLessEqual(len(stream.cache), 4)
        self.assertEqual(list(stream.cache), [6, 7, 8, 9])


if __name__ == '__main__':
    unittest.main()
