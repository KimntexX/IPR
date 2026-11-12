"""Frozen reference membership and conditional burst uncertainty contracts."""
import unittest

import numpy as np

from qualify_same_iso_burst import split_positions
from qualify_photo_pairs import reference_sources
from photo_pair_math import reference_tile
from test_photo_pairs import PROTOCOL


class SameIsoBurstTests(unittest.TestCase):
    def test_alternating_sources_are_disjoint_and_complete(self):
        entries=[dict(source=dict(destination_sha256=str(i))) for i in range(42)]
        refs,sources=split_positions(entries)
        self.assertEqual(refs,entries[::2])
        self.assertEqual(sources,entries[1::2])
        with self.assertRaises(ValueError):split_positions(entries[:-1])
        entries[-1]=entries[0]
        with self.assertRaises(ValueError):split_positions(entries)

    def test_explicit_reference_preserves_actual_iso(self):
        captures=[dict(source_sha256=str(i),iso=4000) for i in range(4)]
        self.assertEqual(reference_sources(dict(reference_source_hashes=['0','2']),captures),{'0','2'})
        self.assertEqual([c['iso'] for c in captures],[4000]*4)
        self.assertEqual(reference_sources({},captures),set())
        for values in ([],['missing'],['0','0'],[None],'0'):
            with self.assertRaises(ValueError):
                reference_sources(dict(reference_source_hashes=values),captures)

    def test_conditional_uncertainty_and_unequal_halves(self):
        rng=np.random.default_rng(92)
        frames=[rng.normal(.1,.0001,(48,48,3)).astype(np.float32) for _ in range(21)]
        _,valid,summary=reference_tile(frames,[[0,0]]*21,[np.ones(3)]*21,PROTOCOL)
        self.assertEqual(summary['half_counts'],[11,10])
        self.assertEqual(summary['valid_pixels'],int(valid.sum()))
        np.testing.assert_allclose(summary['mean_noise_std_if_independent_valid_rgb'],
            np.array(summary['temporal_std_valid_rgb'])/np.sqrt(21),rtol=1e-6)

    def test_empty_valid_support_has_no_nan_estimate(self):
        frames=[np.ones((48,48,3),np.float32) for _ in range(8)]
        _,valid,summary=reference_tile(frames,[[0,0]]*8,[np.ones(3)]*8,PROTOCOL)
        self.assertFalse(valid.any())
        self.assertIsNone(summary['temporal_std_valid_rgb'])
        self.assertIsNone(summary['mean_noise_std_if_independent_valid_rgb'])
        self.assertIsNone(summary['half_difference_rmse_valid_rgb'])


if __name__=='__main__':unittest.main()
