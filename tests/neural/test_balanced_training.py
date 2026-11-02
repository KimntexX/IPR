import unittest
import torch

from audit_balanced_training import paired_summary
from run_synthetic_joint_pilot import metrics


class BalancedTrainingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def row(self, seed, reference_error, candidate_error, exposures):
        target = torch.zeros(1, 3, 128, 128)
        base = target+.1
        return dict(family='weak_achromatic_wave', seed=seed, candidate_exposures=exposures,
            reference=metrics(target, base, target+reference_error),
            candidate=metrics(target, base, target+candidate_error))

    def test_paired_counts_and_exposure_weighted_objective(self):
        rows = [self.row(1, .2, .1, 1), self.row(2, .2, .3, 3)]
        result = paired_summary(rows)
        self.assertEqual(result['informative_cases'], 2)
        self.assertEqual((result['improved'], result['worsened'], result['tied']), (1, 1, 0))
        self.assertAlmostEqual(result['candidate_exposure_weighted_raw_mse']['candidate'], .07, places=6)
        self.assertAlmostEqual(result['candidate']['all']['output_mse'], .05, places=6)
        self.assertEqual(result['candidate_exposures'], 4)
        self.assertAlmostEqual(result['candidate_over_reference_mse'], 1.25, places=6)

    def test_identity_is_tied_and_zero_reference_loss_has_no_ratio(self):
        result = paired_summary([self.row(1, 0., 0., 1)])
        self.assertEqual(result['tied'], 1)
        self.assertIsNone(result['candidate_over_reference_mse'])

    def test_missing_rows_and_zero_exposure_are_rejected(self):
        with self.assertRaises(ValueError):
            paired_summary([])
        with self.assertRaises(ValueError):
            paired_summary([self.row(1, .2, .1, 0)])


if __name__ == '__main__':
    unittest.main()
