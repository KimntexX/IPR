import unittest

from training_exclusions import training_truth_hashes


class TrainingExclusionTests(unittest.TestCase):
    def test_short_stage_does_not_hide_initializer_training_contents(self):
        data = dict(texture_augmentation=dict(events=[dict(target_sha256='active-prefix')]),
            lineage_exclusions=dict(target_sha256=['active-prefix', 'initializer-only']))
        self.assertEqual(training_truth_hashes(data), {'active-prefix', 'initializer-only'})
        self.assertEqual(len(data['texture_augmentation']['events']), 1)

    def test_malformed_ancestor_manifest_is_rejected(self):
        with self.assertRaises(ValueError):
            training_truth_hashes(dict(lineage_exclusions=dict(target_sha256='wrong')))
        with self.assertRaises(ValueError):
            training_truth_hashes(dict(lineage_exclusions=dict(target_sha256=[None])))
