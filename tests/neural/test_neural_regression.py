import unittest

from prepare_neural_regression import replay_plan


class NeuralRegressionTests(unittest.TestCase):
    def test_classic_replay_keeps_failed_data_but_uses_current_models(self):
        models = [dict(seed=s, weights_sha256='new'+str(s)) for s in (71, 73)]
        source = dict(models=models, data=dict(edges=['wrong'], mixed=['wrong'], observations=[], config=dict(width=8)))
        historical = dict(status='completed_independent_noisy_confirmation_not_photographic_qualification',
            groups=[dict(pilot_gates_passed=False)]*6, plan=dict(data=dict(edges=['edge'], mixed=['mixed']), replicas=['fields']))
        plan = replay_plan(source, historical, 'classic')
        self.assertEqual(plan['role'], 'regression')
        self.assertEqual(plan['data']['edges'], ['edge'])
        self.assertEqual(plan['replicas'], ['fields'])
        self.assertEqual(plan['models'], models)
        self.assertNotIn('observations', plan['data'])
        self.assertEqual(source['data']['edges'], ['wrong'])

    def test_fine_replay_preserves_inputs_and_criteria_without_success_requirement(self):
        source = dict(models=[dict(seed=s) for s in (71, 73)], data={})
        historical = dict(status='completed_fine_signal_confirmation_not_photographic_qualification',
            records=[None]*4032, pilot_gates_passed=False,
            plan=dict(cases=['known case'], fields=['known draw'], criteria=dict(threshold=.95), source=dict(models=['old'])))
        plan = replay_plan(source, historical, 'fine')
        self.assertEqual(plan['cases'], historical['plan']['cases'])
        self.assertEqual(plan['criteria'], historical['plan']['criteria'])
        self.assertEqual(plan['source']['models'], source['models'])
        self.assertEqual(plan['role'], 'regression')
        with self.assertRaises(ValueError):
            replay_plan(source, dict(historical, records=[]), 'fine')


if __name__ == '__main__':
    unittest.main()
