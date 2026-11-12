import unittest

from audit_texture_coverage import coverage,bin_index,FREQUENCY


class TextureCoverageTests(unittest.TestCase):
    def test_joint_counts_do_not_treat_clean_as_noisy_or_replay_as_more_contents(self):
        events=[dict(serial=i,family='chromatic_wave',phase=0,frequency=20.,amplitude=.06) for i in range(3)]
        result=coverage(events)
        self.assertEqual(result['positive_joint_cells'],192)
        self.assertEqual(result['missing_cells'],190)
        self.assertEqual(result['singleton_cells'],2)
        with self.assertRaises(ValueError):
            coverage(events+events)

    def test_half_open_bins_and_bad_metadata(self):
        self.assertEqual(bin_index(20.,FREQUENCY),3)
        self.assertEqual(bin_index(6.,FREQUENCY),1)
        for value in (True,float('nan'),29.,1.):
            with self.assertRaises(ValueError):
                bin_index(value,FREQUENCY)
