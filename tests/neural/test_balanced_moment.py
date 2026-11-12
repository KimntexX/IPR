import unittest
import torch

from joint_bayer_unet import sample_rgb
from run_balanced_moment_pilot import balanced_schedule, angular_signal
from synthetic_bayer_data import PATTERNS, noisy_observation


class BalancedMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_schedule_retains_original_training_id_rng_stream(self):
        cases = [dict(pattern=list(PATTERNS[i % 4])) for i in range(7)]
        schedule, counts = balanced_schedule(cases, 20, 4, 32)
        generator = torch.Generator().manual_seed(72)
        actual_counts = torch.zeros_like(counts)
        truth = torch.zeros(4, 3, 32, 32)
        pattern = torch.tensor([PATTERNS[0]]*4)
        for row in schedule:
            ids = torch.randint(len(cases), (4,), generator=generator)
            self.assertEqual(ids.tolist(), row['ids'])
            noisy_observation(truth, pattern, 0., 0., generator)
            for index, phase in zip(row['ids'], row['phases']):
                actual_counts[index, phase] += 1
        self.assertTrue(torch.equal(counts, actual_counts))
        self.assertTrue(((counts.max(1).values-counts.min(1).values) <= 1).all())
        self.assertEqual(int(counts.sum()), 80)
        self.assertEqual(schedule, balanced_schedule(cases, 20, 4, 32)[0])

    def test_duplicate_ids_cycle_within_one_batch(self):
        schedule, counts = balanced_schedule([dict(pattern=list(PATTERNS[2]))], 1, 4, 32)
        self.assertEqual(schedule[0]['phases'], [2, 3, 0, 1])
        self.assertTrue(torch.equal(counts, torch.ones(1, 4, dtype=torch.int64)))

    def test_angular_controls_are_deterministic_cover_bins_and_sample_real_sites(self):
        bins = set()
        hashes = set()
        for i in range(8):
            angle = 11.25+22.5*i
            bins.add(int(angle//45))
            for offset in (-.08, .08):
                truth = angular_signal(angle, offset)
                self.assertTrue(torch.equal(truth, angular_signal(angle, offset)))
                self.assertEqual(truth.shape, (1, 3, 128, 128))
                self.assertTrue(torch.isfinite(truth).all())
                hashes.add(truth.numpy().tobytes())
                packed = sample_rgb(truth, torch.tensor([PATTERNS[0]]))
                self.assertTrue(torch.equal(packed[:, 0], truth[:, 0, ::2, ::2]))
        self.assertEqual(bins, {0, 1, 2, 3})
        self.assertEqual(len(hashes), 16)

    def test_invalid_angular_parameters_rejected(self):
        for angle, offset, side in ((-1, 0, 128), (180, 0, 128), (45, float('nan'), 128), (45, 0, 31)):
            with self.assertRaises(ValueError):
                angular_signal(angle, offset, side)


if __name__ == '__main__':
    unittest.main()
