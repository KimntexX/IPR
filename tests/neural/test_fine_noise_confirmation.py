import copy
import unittest

import torch

from audit_spectral_noise import response
from confirm_fine_noise import fine_signal, reuse_records, summarize


class FineNoiseConfirmationTests(unittest.TestCase):
    def test_signal_and_transfer_without_changing_rng(self):
        state = torch.get_rng_state().clone()
        case = dict(frequency=12, amplitude=.02, angle=.4, phase=.9, chromatic=True)
        image, wave, cosine = fine_signal(case)
        self.assertEqual(image.shape, (1, 3, 192, 192))
        self.assertEqual(response(image, image, wave, cosine)['sine_gain'], [1., 1., 1.])
        self.assertTrue(torch.equal(state, torch.get_rng_state()))
        with self.assertRaises(ValueError):
            fine_signal(dict(case, angle=float('nan')))

    def test_no_failed_observation_is_discarded_on_resume(self):
        field = dict(case_index=0, phase_index=0, noise_index=1, replicate=0,
            observation_sha256='observed', sigma_sha256='sigma')
        row = dict(field, seed=71, target_sha256='target', full=dict(rgb_mse=1.1))
        plan = dict(fields=[field], cases=[dict(target_sha256='target')])
        prior = dict(status='aborted_for_memory_budget', plan_sha256='plan', records=[row])
        self.assertEqual(reuse_records(prior, plan, 'plan')[0]['full']['rgb_mse'], 1.1)
        with self.assertRaises(ValueError):
            reuse_records(prior, plan, 'other')
        with self.assertRaises(ValueError):
            reuse_records(dict(prior, records=[row, row]), plan, 'plan')
        wrong = copy.deepcopy(prior)
        wrong['records'][0]['sigma_sha256'] = 'changed'
        with self.assertRaises(ValueError):
            reuse_records(wrong, plan, 'plan')

    def test_group_reports_each_channel_and_sensor_error(self):
        criteria = dict(strong_minimum_channel_gain_over_parent=.95, strong_rgb_and_sensor_maximum_ratio=1.000001)
        row = dict(full=dict(sine_gain=[1., 1., 1.], rgb_mse=.8),
            parent=dict(sine_gain=[1., 1., 1.], rgb_mse=1.), sensor=dict(corrected_mse=.5, noisy_mse=1.))
        self.assertTrue(summarize([row], criteria)['strong_passed'])
        wrong = copy.deepcopy(row)
        wrong['full']['sine_gain'][2] = .8
        self.assertFalse(summarize([wrong], criteria)['strong_checks']['channel_transfer'])
        wrong = copy.deepcopy(row)
        wrong['sensor']['corrected_mse'] = 1.2
        self.assertFalse(summarize([wrong], criteria)['strong_checks']['sensor_not_worse'])


if __name__ == '__main__':
    unittest.main()
