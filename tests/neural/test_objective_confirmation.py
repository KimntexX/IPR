import copy
import unittest

import torch

from compare_reconstruction_objectives import freeze_cases, model_contract, paired


class ObjectiveConfirmationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_paired_contract_rejects_selection_and_configuration_drift(self):
        proofs = [dict(status='completed_local_route_pilot_not_noise_training',
            freeze=dict(config=dict(seed=s, model_kind='local', steps=2048, width=8,
                **({'objective': o} if o == 'relative' else {}))))
            for o in ('rgb', 'relative') for s in (71, 73)]
        self.assertEqual(set(model_contract(proofs)), {(o, s) for o in ('rgb', 'relative') for s in (71, 73)})
        for bad in (proofs[:3], proofs[:3]+[proofs[0]]):
            with self.assertRaises(ValueError):
                model_contract(bad)
        bad = copy.deepcopy(proofs)
        bad[-1]['freeze']['config']['width'] = 16
        with self.assertRaises(ValueError):
            model_contract(bad)
        six = copy.deepcopy(proofs)+copy.deepcopy(proofs[2:])
        for p in six[-2:]:
            p['freeze']['config']['objective'] = 'tempered'
        self.assertEqual(len(model_contract(six)), 6)

    def test_new_targets_cover_strata_and_reject_seen_truth(self):
        source = dict(freeze=dict(train_edge_events=[], train_slots=[], evaluation_cases=[]))
        prior = dict(edges=[], mixed=[])
        edges, mixed = freeze_cases(source, prior)
        self.assertEqual({e['seed'] for e in edges}, set(range(2000160, 2000192)))
        self.assertEqual({e['normal_bin'] for e in edges}, set(range(32)))
        self.assertEqual(len({c['target_sha256'] for c in edges+mixed}), 52)
        self.assertTrue(all(len(c['packed_sha256_by_phase']) == 4 for c in edges+mixed))
        prior['mixed'] = [mixed[0]]
        with self.assertRaises(ValueError):
            freeze_cases(source, prior)
        edges2, mixed2 = freeze_cases(source, dict(edges=edges, mixed=mixed), 192, 3300000)
        self.assertFalse({r['target_sha256'] for r in edges+mixed}.intersection(r['target_sha256'] for r in edges2+mixed2))

    def test_pairing_reports_both_directions_and_rejects_identity_mismatch(self):
        def row(error):
            return dict(family='edge', seed=12, phase_index=0, target_sha256='truth', packed_sha256='observed',
                metrics=dict(pixels=1, baseline_sse=9., output_sse=error,
                    baseline_opponent_sse=6., output_opponent_sse=error,
                    reference_gradient_energy=1., baseline_gradient_projection=1., output_gradient_projection=1.,
                    channel_bias_sum=[0., 0., 0.]))
        records = [dict(seed=s, objective=o, mixed_records=[row(e)], edge_records=[row(e)])
            for s, o, e in ((71, 'rgb', 3.), (71, 'relative', 1.), (73, 'rgb', 3.), (73, 'relative', 6.))]
        scores = [r for r in paired(records) if r['group'] == 'mixed_records' and r['family'] == 'all']
        self.assertEqual(scores[0]['better'], 1)
        self.assertEqual(scores[1]['worse'], 1)
        self.assertAlmostEqual(scores[0]['output_mse_ratio_candidate_to_rgb'], 1/3)
        records[-1]['edge_records'][0]['target_sha256'] = 'other'
        with self.assertRaises(ValueError):
            paired(records)


if __name__ == '__main__':
    unittest.main()
