import unittest

from summarise_noise_rebaseline import compare, compare_by_iso, distribution


def campaign(scale=1, drift=0, identity='same'):
    return {'pairs': [{'a': 'A', 'b': 'B', 'a_sha256': identity, 'b_sha256': 'b',
                       'mean_y_drift_a_vs_b': drift,
                       'bins': [{'bin': 3, 'planes': {'y': {
                           'haar_hh': {'truth_median': scale, 'blind_q25': .9 * scale},
                           'mid': {'truth_median': 2 * scale, 'blind_q25': 1.8 * scale}}}}]}]}


class RebaselineTests(unittest.TestCase):
    def test_matching_sources_and_spectral_ratios(self):
        summary = compare(campaign(), campaign(.5))['summary']['y']
        self.assertEqual(summary['mid']['after_over_before_sigma']['median'], .5)
        self.assertEqual(summary['mid']['after_band_over_haar']['median'], 2)
        self.assertEqual(summary['mid']['after_blind_q25_over_truth']['median'], .9)

    def test_different_source_hash_is_not_matched(self):
        self.assertEqual(compare(campaign(), campaign(identity='changed'))['matched_observations'], 0)

    def test_drift_is_excluded_without_changing_source(self):
        record = campaign(drift=.03)
        self.assertEqual(compare(campaign(), record)['matched_observations'], 0)
        self.assertEqual(record['pairs'][0]['mean_y_drift_a_vs_b'], .03)

    def test_nearest_rank_quartiles_and_empty_distribution(self):
        self.assertEqual(distribution([4, 1, 3, 2]), {'count': 4, 'median': 2.5, 'q1': 1, 'q3': 3})
        self.assertEqual(distribution([]), {'count': 0})

    def test_iso_comes_from_metadata_not_folder(self):
        audit = {'groups': [{'folder': 'iso-3200', 'frames': [
            {'sha256': key, 'metadata': {'iso': 1600}} for key in ('same', 'b')]}]}
        self.assertEqual(list(compare_by_iso(campaign(), campaign(), audit)), ['1600'])

    def test_zero_truth_is_not_reported_as_a_zero_noise_ratio(self):
        self.assertEqual(compare(campaign(), campaign(0))['matched_observations'], 0)


if __name__ == '__main__':
    unittest.main()
