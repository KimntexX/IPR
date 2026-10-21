import unittest
import numpy as np
from qualify_headroom_candidates import compare_candidate


class CandidateTests(unittest.TestCase):
    def test_identity_has_no_change(self):
        pixels=np.full((8,8,3),12345,dtype=np.uint16)
        stats=compare_candidate(pixels,pixels)
        self.assertEqual(stats['global_grid']['delta_E00_mean'],0)
        self.assertEqual(stats['global_grid']['signed_Y_change'],0)
        self.assertEqual(stats['global_grid']['chromatic_samples'],0)
        self.assertIsNone(stats['global_grid']['hue_change_degrees_p95'])

    def test_native_precision_and_geometry_are_required(self):
        pixels=np.zeros((8,8,3),dtype=np.uint16)
        with self.assertRaises(ValueError):compare_candidate(pixels,pixels.astype(np.uint8))
        with self.assertRaises(ValueError):compare_candidate(pixels,pixels[:4])

    def test_brightening_is_positive_and_not_hidden(self):
        baseline=np.full((8,8,3),10000,dtype=np.uint16)
        candidate=np.full((8,8,3),11000,dtype=np.uint16)
        stats=compare_candidate(baseline,candidate)['global_grid']
        self.assertGreater(stats['delta_E00_mean'],0)
        self.assertGreater(stats['signed_Y_change'],0)


if __name__=='__main__':unittest.main()
