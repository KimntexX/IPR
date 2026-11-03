import unittest

import torch

from audit_spectral_noise import periodic_signal, response, source_contract


class SpectralNoiseTests(unittest.TestCase):
    def test_diagnosis_rejects_incomplete_confirmation_and_unfinished_study(self):
        from pathlib import Path
        proof = dict(status='completed_independent_noisy_confirmation_not_photographic_qualification',
            pilot_gates_passed=True, groups=[dict(evaluation=dict(pilot_gates_passed=True))]*6,
            plan=dict(kind='protected'))
        self.assertEqual(source_contract(Path.cwd(), proof)['kind'], 'protected')
        with self.assertRaises(ValueError):
            source_contract(Path.cwd(), dict(proof, groups=proof['groups'][:5]))
        with self.assertRaises(ValueError):
            source_contract(Path.cwd(), dict(status='incomplete'))

    def test_failed_development_probe_is_diagnosable_but_weights_are_pinned(self):
        import copy
        import json
        from pathlib import Path
        import tempfile
        from diagnose_train_learnability import digest
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder = root/'build-patch_neural_restoration'
            folder.mkdir()
            path = folder/'plan.json'
            path.write_text(json.dumps(dict(config=dict(width=8))), encoding='utf-8')
            refs = [dict(seed=s, weights_sha256='w'+str(s), result_sha256='r'+str(s)) for s in (71, 73)]
            proof = dict(status='completed_fixed_edge_protection_probe_not_photographic_qualification',
                pilot_gates_passed=False, plan=dict(data_plan_path=str(path), data_plan_sha256=digest(path),
                    models=refs, tools={}), models=[dict(seed=r['seed'], learned_weights_sha256=r['weights_sha256'],
                        source_result_sha256=r['result_sha256']) for r in refs])
            self.assertEqual(source_contract(root, proof)['data']['config']['width'], 8)
            wrong = copy.deepcopy(proof)
            wrong['models'][0]['learned_weights_sha256'] = 'other'
            with self.assertRaises(ValueError):
                source_contract(root, wrong)
            path.write_text('{}', encoding='utf-8')
            with self.assertRaises(ValueError):
                source_contract(root, proof)

    def test_response_recovers_each_channel_attenuation_phase_and_offset(self):
        truth, wave, cosine = periodic_signal(16, .02, 2, True)
        expected = torch.tensor((.8, 1.1, .9))
        altered = .3+expected[None, :, None, None]*(truth-.3)+.004
        measured = response(altered, truth, wave, cosine)
        torch.testing.assert_close(torch.tensor(measured['sine_gain']), expected, atol=2e-6, rtol=0)
        torch.testing.assert_close(torch.tensor(measured['dc_error']), torch.full((3,), .004), atol=2e-7, rtol=0)
        self.assertLess(max(map(abs, measured['quadrature_gain'])), 2e-6)
        self.assertGreater(measured['rgb_mse'], 0.)

    def test_exact_truth_and_rejection(self):
        truth, wave, cosine = periodic_signal(4, .005, 0, False)
        measured = response(truth, truth, wave, cosine)
        self.assertEqual(measured['rgb_mse'], 0.)
        self.assertEqual(measured['sine_gain'], [1., 1., 1.])
        with self.assertRaises(ValueError):
            periodic_signal(64, .005, 0, False)
        with self.assertRaises(ValueError):
            response(truth*float('nan'), truth, wave, cosine)


if __name__ == '__main__':
    unittest.main()
