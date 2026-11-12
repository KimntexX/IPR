import unittest
import torch
from dru_training_data import example,core_loss


class StreamingDataTests(unittest.TestCase):
    def test_full_context_is_reproducible_canonical_and_noise_free_identity(self):
        torch.set_num_threads(4)
        a=example(0,71,[{}]);b=example(0,71,[{}])
        for first,second in zip(a,b):self.assertTrue(torch.equal(first,second))
        packed,sigma,tags,target=a
        self.assertEqual(packed.shape,(1,4,512,512));self.assertEqual(tags.tolist(),[[0,1,3,2]])
        self.assertTrue(torch.equal(packed,target));self.assertEqual(float(sigma.sum()),0.)

    def test_noise_normalization_and_core_exclude_halo(self):
        target=torch.zeros(1,4,512,512);sigma=torch.full_like(target,.02)
        prediction=target.clone();prediction[:,:,:192]=3
        self.assertEqual(float(core_loss(prediction,target,sigma)),0.)
        prediction[:,:,192:320,192:320]=.02
        self.assertAlmostEqual(float(core_loss(prediction,target,sigma)),1.,places=5)

    def test_under_context_and_invalid_identity_refused(self):
        with self.assertRaises(ValueError):example(0,71,[{}],512)
        with self.assertRaises(ValueError):example(True,71,[{}])


if __name__=='__main__':unittest.main()
