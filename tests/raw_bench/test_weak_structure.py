import unittest
from study_weak_structure import summarize


class StructureSummaryTests(unittest.TestCase):
    def cases(self):
        return [dict(pattern=pattern,
                     clean_100=dict(pattern_gain=.8, models=[dict(pooled_sigma_I=.001)]),
                     noisy_100=dict(rgb_mse=2), noisy_zero=dict(rgb_mse=4))
                for pattern in ('horizontal', 'vertical', 'diagonal', 'product') for _ in range(8)]

    def test_diagnostic_keeps_patterns_and_counts(self):
        output = summarize(dict(cases=self.cases()))
        self.assertEqual(len(output), 4)
        for group in output:
            self.assertEqual(group['cases'], 8)
            self.assertEqual(group['clean_cases_with_nonzero_noise_estimate'], 8)
            self.assertEqual(group['minimum_clean_gain'], .8)
            self.assertEqual(group['noisy_mse_ratios'], [.5] * 8)

    def test_missing_cases_fail(self):
        with self.assertRaises(ValueError):
            summarize(dict(cases=self.cases()[:-1]))

    def test_zero_estimation_is_not_contamination(self):
        rows = self.cases()
        for row in rows:
            row['clean_100']['models'][0]['pooled_sigma_I'] = 0
        self.assertTrue(all(r['clean_cases_with_nonzero_noise_estimate'] == 0
                            for r in summarize(dict(cases=rows))))


if __name__ == '__main__':
    unittest.main()
