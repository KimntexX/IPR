import unittest
import torch

from run_diverse_moment_pilot import edge_recipe, edge_image, freeze_edge, validate_edge_exposures
from joint_bayer_unet import sample_rgb
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


class DiverseMomentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_both_partitions_cover_all_strata_and_are_disjoint(self):
        hashes = {}
        for split in ('train', 'reserved'):
            recipes = [edge_recipe(i, split) for i in range(32)]
            self.assertEqual({r['normal_bin'] for r in recipes}, set(range(32)))
            self.assertEqual(recipes, [edge_recipe(i, split) for i in range(32)])
            self.assertTrue(all(r['normal_bin']*180/32 < r['normal_degrees'] < (r['normal_bin']+1)*180/32 for r in recipes))
            hashes[split] = {tensor_hash(edge_image(r)) for r in recipes}
            self.assertEqual(len(hashes[split]), 32)
        self.assertFalse(hashes['train'] & hashes['reserved'])

    def test_freeze_hashes_exact_truth_and_each_actual_mosaic(self):
        recipe = edge_recipe(7, 'train')
        frozen = freeze_edge(recipe, 128)
        truth = edge_image(recipe)
        self.assertEqual(truth.shape, (1, 3, 128, 128))
        self.assertEqual(frozen['target_sha256'], tensor_hash(truth))
        self.assertGreaterEqual(float(truth.min()), .03)
        self.assertLessEqual(float(truth.max()), .97)
        for i, p in enumerate(PATTERNS):
            self.assertEqual(frozen['packed_sha256_by_phase'][i], tensor_hash(sample_rgb(truth, torch.tensor([p]))))

    def test_straight_edge_uses_both_declared_rgb_colours(self):
        recipe = edge_recipe(4, 'reserved')
        image = edge_image(recipe)
        colours = image[0].permute(1, 2, 0).reshape(-1, 3).unique(dim=0)
        self.assertEqual(len(colours), 2)
        for value in (recipe['low_rgb'], recipe['high_rgb']):
            self.assertTrue((colours == torch.tensor(value)).all(dim=1).any())

    def test_invalid_partition_and_nonfinite_parameters_rejected(self):
        for index, split in ((-1, 'train'), (1, 'unknown')):
            with self.assertRaises(ValueError):
                edge_recipe(index, split)
        for replacement in (dict(offset=float('nan')), dict(normal_degrees=180), dict(high_rgb=[float('inf')]*3)):
            with self.assertRaises(ValueError):
                edge_image(dict(edge_recipe(0, 'train'), **replacement))

    def test_each_training_recipe_visits_distinct_phases_and_accepts_partial_tail(self):
        events = [dict(recipe_index=0, phase_index=p) for p in (2, 3, 0, 1)]
        validate_edge_exposures(events+[dict(recipe_index=1, phase_index=3)])
        with self.assertRaises(ValueError):
            validate_edge_exposures(events+[dict(recipe_index=0, phase_index=3)])
        with self.assertRaises(ValueError):
            validate_edge_exposures([])


if __name__ == '__main__':
    unittest.main()
