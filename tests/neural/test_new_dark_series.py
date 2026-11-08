import unittest
import numpy as np
from qualify_new_dark_series import split_sources,compare_groups,assess


class NewDarkTests(unittest.TestCase):
    def test_split_is_chronological_not_metric_dependent(self):
        rows=[dict(path=str(i),metadata=dict(iso=100,exposure_s=.005,datetime_original=f'2026:11:08 10:{i:02d}:00'),capture_mode=dict(mode='RAW')) for i in range(52)]
        a,b=split_sources(list(reversed(rows)),100,'2026:11:08')
        self.assertEqual([r['path'] for r in a],list(map(str,range(32))))
        self.assertEqual(len(b),20)
        with self.assertRaises(ValueError):split_sources(rows[:40],100,'2026:11:08')

    def test_unrelated_identical_noise_population_passes_repeatability(self):
        rng=np.random.default_rng(37)
        a=rng.normal(512,2,(32,1,4,32,32));b=rng.normal(512,2,(20,1,4,32,32))
        result=compare_groups(a,b)
        self.assertLess(np.median([r['relative_variance_difference'] for r in result['rows']]),.05)

    def test_changed_variance_is_not_qualified(self):
        rng=np.random.default_rng(39)
        a=rng.normal(512,2,(32,1,4,32,32));b=rng.normal(512,4,(20,1,4,32,32))
        self.assertFalse(assess(compare_groups(a,b)['rows'])['passed'])


if __name__=='__main__':unittest.main()
