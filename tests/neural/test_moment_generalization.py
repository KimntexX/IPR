import unittest
import torch

from audit_moment_generalization import edge_regions, regional_metrics, summarize_regions, edge_orientation
from run_synthetic_joint_pilot import metrics


class MomentGeneralizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_two_sided_transition_and_disjoint_partition(self):
        target = torch.zeros(1, 3, 128, 128)
        target[:, :, :, 64:] = 1
        regions = edge_regions(target)
        band = regions['near_edge']
        self.assertEqual(int(band.sum()), 8*32)
        self.assertTrue(band[:, :, :, 12:20].all())
        self.assertTrue(torch.equal(~band, regions['away_from_edge']))
        self.assertFalse(band[:, :, :, :12].any())

    def test_dilation_precedes_core_crop_and_constant_has_no_edge(self):
        target = torch.zeros(1, 3, 128, 128)
        target[:, :, :, 48:] = .5
        band = edge_regions(target)['near_edge']
        self.assertTrue(band[:, :, :, :4].all())
        self.assertFalse(band[:, :, :, 4:].any())
        self.assertFalse(edge_regions(torch.ones_like(target))['near_edge'].any())

    def test_regional_error_mass_matches_full_metric_and_empty_is_explicit(self):
        generator = torch.Generator().manual_seed(93)
        target = torch.zeros(1, 3, 128, 128)
        target[:, :, 64:, :] = .7
        base = target + torch.rand(target.shape, generator=generator)*.1
        output = target + torch.rand(target.shape, generator=generator)*.05
        regions = {name: regional_metrics(target, base, output, mask)
                   for name, mask in edge_regions(target).items()}
        full = metrics(target, base, output)
        for key in ('pixels', 'baseline_sse', 'output_sse', 'baseline_opponent_sse', 'output_opponent_sse'):
            self.assertAlmostEqual(sum(r[key] for r in regions.values()), full[key], places=5)
        blank = torch.zeros(1, 1, 32, 32, dtype=torch.bool)
        result = summarize_regions([dict(regions=dict(empty=regional_metrics(target, base, output, blank)))], 'empty')
        self.assertEqual(result['pixels'], 0)
        self.assertIsNone(result['output_mse'])
        self.assertIsNone(result['mse_ratio'])

    def test_orientation_is_fixed_and_invalid_masks_are_rejected(self):
        angle, group = edge_orientation(7500)
        self.assertTrue(0 <= angle < 180)
        self.assertEqual(group, int(angle//45))
        self.assertEqual(edge_orientation(7500), (angle, group))
        target = torch.zeros(1, 3, 128, 128)
        for bad in (target*float('nan'), target[:, :, :80], target[:, :2]):
            with self.assertRaises(ValueError):
                edge_regions(bad)
        with self.assertRaises(ValueError):
            regional_metrics(target, target, target, torch.zeros(1, 1, 32, 32))


if __name__ == '__main__':
    unittest.main()
