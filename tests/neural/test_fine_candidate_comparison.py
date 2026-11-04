import copy
import unittest

from compare_fine_candidates import compare, moment_estimates


def proof():
    fields = [dict(case_index=case, phase_index=phase, noise_index=level, replicate=replicate)
        for case in range(72) for phase in range(4) for level in range(3)
        for replicate in range(1 if level == 0 else 3)]
    rows = [dict(field, seed=seed, target_sha256='target'+str(field['case_index']),
        observation_sha256=str(field), sigma_sha256='sigma'+str(field),
        parent=dict(rgb_mse=1.), full=dict(rgb_mse=.8, sine_gain=[1., 1., 1.], quadrature_gain=[0., 0., 0.]))
        for seed in (71, 73) for field in fields]
    groups = [dict(seed=seed, noise_index=level, amplitude=.06, strong_passed=True,
        gain_over_parent=[1., 1., 1.]) for seed in (71, 73) for level in (1, 2) for _ in range(12)]
    return dict(status='completed_fine_signal_regression_not_photographic_qualification',
        plan=dict(cases=[dict(amplitude=.06, chromatic=bool(i%2)) for i in range(72)],
            fields=fields, criteria=dict(strong_amplitude=.06)),
        records=rows, groups=groups, pilot_gates_passed=True)


class FineCandidateComparisonTests(unittest.TestCase):
    def test_finite_sample_bias_correction_is_not_clipped_and_decomposition_closes(self):
        moments = moment_estimates([-1., 0., 1.], 0.)
        self.assertAlmostEqual(moments['bias_squared_estimate'], -1/3)
        self.assertAlmostEqual(moments['mean_squared_error'],
            moments['sample_variance']+moments['bias_squared_estimate'])
        with self.assertRaises(ValueError):
            moment_estimates([0., 1.], 0.)

    def test_reports_quality_tradeoff_without_overriding_gate(self):
        old = proof()
        new = copy.deepcopy(old)
        new['groups'][0].update(strong_passed=False, gain_over_parent=[.8, 1., 1.])
        new['pilot_gates_passed'] = False
        for row in new['records']:
            row['full']['rgb_mse'] = .4
        result = compare(old, new)
        self.assertFalse(result['current_passed'])
        self.assertEqual(result['comparisons'][0]['current_strong_failed'], 1)
        self.assertAlmostEqual(result['comparisons'][0]['current_over_previous_rgb_mse'], .5)
        self.assertEqual(result['comparisons'][0]['current_minimum_transfer'], .8)

    def test_rejects_unpaired_or_incomplete_measurements(self):
        old = proof()
        for modification in ('noise', 'missing', 'duplicate', 'parent', 'criteria'):
            new = copy.deepcopy(old)
            if modification == 'noise':
                new['records'][0]['observation_sha256'] = 'different'
            elif modification == 'missing':
                new['records'].pop()
            elif modification == 'duplicate':
                new['records'][-1] = new['records'][0]
            elif modification == 'parent':
                new['records'][0]['parent']['rgb_mse'] = 2.
            else:
                new['plan']['criteria']['strong_amplitude'] = .07
            with self.subTest(modification=modification), self.assertRaises(ValueError):
                compare(old, new)
