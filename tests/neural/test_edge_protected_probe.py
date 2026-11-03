import copy
import unittest

from run_edge_protected_probe import compare_records


class ProtectedProbeTests(unittest.TestCase):
    def test_pairs_keep_distinct_edge_regression_and_reject_changed_observation(self):
        def row(level, mixed, error):
            return dict(target_sha256=f'{level}-{mixed}', noisy_sha256='noise', sigma_sha256='sigma',
                phase_index=0, noise_index=level, mixed=mixed, output_sha256=dict(parent='fixed-parent'),
                metrics=dict(full=dict(pixels=1, baseline_sse=9., output_sse=error,
                    baseline_opponent_sse=6., output_opponent_sse=error, reference_gradient_energy=1.,
                    baseline_gradient_projection=1., output_gradient_projection=1., channel_bias_sum=[0., 0., 0.])))
        old = [row(level, mixed, 3.) for level in range(3) for mixed in (True, False)]
        new = [row(level, mixed, 4. if level == 2 and not mixed else 1.) for level in range(3) for mixed in (True, False)]
        scores = compare_records(new, old)
        distinct = next(r for r in scores if r['noise_index'] == 2 and r['group'] == 'distinct_edges')
        whole = next(r for r in scores if r['noise_index'] == 2 and r['group'] == 'all')
        self.assertEqual(distinct['worse'], 1)
        self.assertGreater(distinct['mse_ratio_protected_over_unprotected'], 1)
        self.assertLess(whole['mse_ratio_protected_over_unprotected'], 1)
        wrong = copy.deepcopy(new)
        wrong[0]['sigma_sha256'] = 'changed'
        with self.assertRaises(ValueError):
            compare_records(wrong, old)


if __name__ == '__main__':
    unittest.main()
