import copy
import unittest

from continuation_policy import secondary_weight, completed_training


class ContinuationPolicyTests(unittest.TestCase):
    def test_declared_transition_endpoints_and_resume_step(self):
        config = dict(steps=512, structure_schedule=dict(start=0., end=1., ramp_steps=256))
        self.assertEqual(secondary_weight(config, 1), 0.)
        self.assertEqual(secondary_weight(config, 256), 1.)
        self.assertEqual(secondary_weight(config, 512), 1.)
        self.assertAlmostEqual(secondary_weight(config, 128), 127/255)
        config['structure_schedule']['end'] = 0.
        self.assertTrue(all(secondary_weight(config, s) == 0 for s in (1, 128, 256, 512)))
        config['structure_schedule']['ramp_steps'] = 0
        with self.assertRaises(ValueError):
            secondary_weight(config, 1)

    def test_continuation_requires_both_real_stage_and_cumulative_counters(self):
        ref = dict(seed=71, weights_sha256='warm', result_sha256='proof')
        proof = dict(seed=71, plan=dict(config=dict(steps=512), warm_start=dict(
            completed_optimizer_steps=2048, models=[ref])), warm_start=ref,
            training_progress=dict(completed_optimizer_steps=512, total_optimizer_steps=2560))
        self.assertTrue(completed_training(proof))
        for field, value in (('completed_optimizer_steps', 511), ('total_optimizer_steps', 512)):
            bad = copy.deepcopy(proof)
            bad['training_progress'][field] = value
            self.assertFalse(completed_training(bad))
        bad = copy.deepcopy(proof)
        bad['warm_start']['weights_sha256'] = 'wrong'
        # The source declaration must be an independent snapshot.
        bad['plan'] = copy.deepcopy(proof['plan'])
        self.assertFalse(completed_training(bad))
        self.assertFalse(completed_training(dict(plan=dict(config=dict(steps=512)),
            training_progress=dict(completed_optimizer_steps=512))))
