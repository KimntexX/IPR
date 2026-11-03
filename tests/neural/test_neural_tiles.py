import unittest

import torch

from audit_neural_tiles import centered_origin, compare_core, validate_capture_coverage


class NeuralTileTests(unittest.TestCase):
    def test_capture_coverage_rejects_missing_and_duplicate_rois(self):
        import copy
        sources = [dict(source_sha256=str(i), rgb_replay_bit_exact=True, rgb_regions_compared=9,
            stages=[dict(stage=name, roi=r) for name in ('normalized_cfa_before_wb_unclipped',
                'balanced_cfa_pre_rcd_white_clipped') for r in range(9)]) for i in range(3)]
        validate_capture_coverage(sources)
        broken = copy.deepcopy(sources)
        broken[0]['stages'][0]['roi'] = 1
        with self.assertRaises(ValueError):
            validate_capture_coverage(broken)
        with self.assertRaises(ValueError):
            validate_capture_coverage(sources[:2])
        broken = copy.deepcopy(sources)
        broken[0]['rgb_replay_bit_exact'] = False
        with self.assertRaises(ValueError):
            validate_capture_coverage(broken)

    def test_core_alignment_and_deliberately_wrong_crop(self):
        full = torch.arange(384*384, dtype=torch.float32).reshape(1, 1, 384, 384).expand(1, 3, -1, -1)
        for side in (128, 160, 192):
            origin = centered_origin(384, side)
            tile = full[:, :, origin:origin+side, origin:origin+side]
            score = compare_core(full, tile)
            self.assertTrue(score['bit_exact'])
            self.assertEqual(score['max_absolute_difference'], 0.)
            wrong = full[:, :, origin+2:origin+side+2, origin:origin+side]
            self.assertGreater(compare_core(full, wrong)['max_absolute_difference'], 0.)

    def test_rejects_pool_grid_shift_and_nonfinite_view(self):
        with self.assertRaises(ValueError):
            centered_origin(384, 136)
        full = torch.zeros(1, 3, 384, 384)
        tile = torch.zeros(1, 3, 160, 160)
        tile[:, :, 80, 80] = float('nan')
        with self.assertRaises(ValueError):
            compare_core(full, tile)


if __name__ == '__main__':
    unittest.main()
