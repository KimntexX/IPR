import copy
import unittest

from confirm_noisy_model import eligibility, reused_groups


class NoisyConfirmationTests(unittest.TestCase):
    def test_plain_eligibility_requires_both_successful_seeds(self):
        refs = [dict(seed=s, result_sha256=str(s), weights_sha256='w'+str(s)) for s in (71, 73)]
        proof = dict(status='completed_both_seeds', pilot_gates_passed=True, attempts=[
            dict(status='completed_synthetic_noise_pilot_not_photographic_qualification', seed=s,
                result_sha256=str(s), pilot_gates_passed=True) for s in (71, 73)])
        eligibility('plain', proof, refs)
        wrong = copy.deepcopy(proof)
        wrong['attempts'][-1]['pilot_gates_passed'] = False
        with self.assertRaises(ValueError):
            eligibility('plain', wrong, refs)
        with self.assertRaises(ValueError):
            eligibility('plain', proof, refs[:1])

    def test_protected_eligibility_pins_weights_and_preserves_failure(self):
        refs = [dict(seed=s, result_sha256=str(s), weights_sha256='w'+str(s)) for s in (71, 73)]
        proof = dict(status='completed_fixed_edge_protection_probe_not_photographic_qualification',
            pilot_gates_passed=True, models=[dict(seed=s, source_result_sha256=str(s),
                learned_weights_sha256='w'+str(s), pilot_gates_passed=True) for s in (71, 73)])
        eligibility('protected', proof, refs)
        wrong = copy.deepcopy(proof)
        wrong['models'][0]['learned_weights_sha256'] = 'other'
        with self.assertRaises(ValueError):
            eligibility('protected', wrong, refs)
        with self.assertRaises(ValueError):
            eligibility('protected', dict(proof, pilot_gates_passed=False), refs)

    def test_reuse_keeps_failed_group_and_rejects_changed_data_or_plan(self):
        observations = [dict(case_index=i//12, phase=(i//3)%4, noise_index=i%3,
            noisy_sha256='n'+str(i), sigma_sha256='s'+str(i)) for i in range(624)]
        records = [dict(case_index=f['case_index'], phase_index=f['phase'], noise_index=f['noise_index'],
            noisy_sha256=f['noisy_sha256'], sigma_sha256=f['sigma_sha256'], target_sha256='t'+str(f['case_index'])) for f in observations]
        plan = dict(data=dict(mixed=[dict(target_sha256='t'+str(i)) for i in range(20)],
            edges=[dict(target_sha256='t'+str(i)) for i in range(20, 52)]),
            models=[dict(seed=s, weights_sha256='w'+str(s)) for s in (71, 73)],
            replicas=[dict(index=i, manifest_sha256='m'+str(i), observations=observations) for i in range(3)])
        group = dict(seed=71, replicate=0, weights_sha256='w71', manifest_sha256='m0',
            evaluation=dict(records=records, pilot_gates_passed=False))
        prior = dict(status='aborted_for_memory_budget', plan_sha256='plan', groups=[group])
        self.assertFalse(reused_groups(prior, plan, 'plan')[0]['evaluation']['pilot_gates_passed'])
        with self.assertRaises(ValueError):
            reused_groups(prior, plan, 'different')
        wrong = copy.deepcopy(prior)
        wrong['groups'][0]['evaluation']['records'][0]['sigma_sha256'] = 'different'
        with self.assertRaises(ValueError):
            reused_groups(wrong, plan, 'plan')
        with self.assertRaises(ValueError):
            reused_groups(dict(prior, groups=[group, group]), plan, 'plan')


if __name__ == '__main__':
    unittest.main()
