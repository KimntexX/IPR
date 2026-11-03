import copy
import unittest

import torch

from run_noisy_moment_pilot import observation, noise_verdict, NOISE, MEMORY_POLICY
from memory_budget import MemoryGuard, violation
from synthetic_bayer_data import FAMILIES, PATTERNS


class NoisyProtocolTests(unittest.TestCase):
    def test_authorized_system_threshold_keeps_process_limits(self):
        guard = MemoryGuard(**MEMORY_POLICY)
        self.assertEqual(guard.system_fraction, .92)
        self.assertEqual(guard.hard_bytes, 2*1024**3)
        self.assertEqual(guard.soft_bytes, 1280*1024**2)
        sample = dict(process_private_bytes=100, process_rss_bytes=100, system_used_fraction=.90)
        self.assertIsNone(violation(sample, guard.soft_bytes, guard.system_fraction))
        self.assertEqual(violation(dict(sample, system_used_fraction=.92), guard.soft_bytes, guard.system_fraction),
            'system_physical_memory_threshold')
        self.assertEqual(violation(dict(sample, process_private_bytes=guard.soft_bytes), guard.soft_bytes, guard.system_fraction),
            'process_soft_memory_limit')

    def test_observation_is_repeatable_and_sigma_uses_observed_signal(self):
        truth = torch.full((1, 3, 32, 40), -.001)
        tags = torch.tensor([PATTERNS[0]])
        state = torch.get_rng_state().clone()
        noisy, sigma = observation(truth, tags, 2, 94000000)
        other, other_sigma = observation(truth, tags, 2, 94000000)
        self.assertTrue(torch.equal(noisy, other))
        self.assertTrue(torch.equal(sigma, other_sigma))
        a, b = NOISE[2]
        self.assertTrue(torch.allclose(sigma.square(), a*noisy.clamp_min(0)+b, atol=1e-10))
        self.assertFalse(torch.equal(noisy, observation(truth, tags, 2, 94000001)[0]))
        self.assertTrue(torch.equal(state, torch.get_rng_state()))
        clean, zero = observation(truth, tags, 0, 94000000)
        self.assertTrue((clean == -.001).all())
        self.assertTrue((zero == 0).all())

    def test_verdict_does_not_hide_chroma_or_structure_regressions(self):
        def metric(error):
            return dict(pixels=1, baseline_sse=9., output_sse=error,
                baseline_opponent_sse=6., output_opponent_sse=error,
                reference_gradient_energy=1., baseline_gradient_projection=1.,
                output_gradient_projection=1., channel_bias_sum=[0., 0., 0.])
        rows = [dict(family=f, noise_index=level,
            metrics={v: metric(error) for v, error in (('parent', 3.), ('full', 1.), ('without_correction', 2.))})
            for level in (1, 2) for f in FAMILIES]
        self.assertTrue(all(v['passed'] for v in noise_verdict(rows)))
        bad = copy.deepcopy(rows)
        for row in bad:
            row['metrics']['full']['output_opponent_sse'] = 4.
        self.assertTrue(all(not v['checks']['opponent_not_worse_vs_parent'] for v in noise_verdict(bad)))
        bad = copy.deepcopy(rows)
        for row in bad:
            row['metrics']['without_correction']['output_sse'] = .5
            if row['family'] == 'edge':
                row['metrics']['full']['output_gradient_projection'] = .8
        verdict = noise_verdict(bad)
        self.assertTrue(all(not v['checks']['correction_contributes'] for v in verdict))
        self.assertTrue(all(not v['checks']['edge_gradient'] for v in verdict))


if __name__ == '__main__':
    unittest.main()
