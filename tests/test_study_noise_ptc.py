import unittest
from study_noise_ptc import fit, analyze


def synthetic_audit():
    offsets = ([0, 0], [0, 1], [1, 0], [1, 1])
    frames, pairs = [], []
    for level, signal in enumerate((0, 10, 30, 100)):
        paths = [f'frame-{level}-{side}' for side in ('a', 'b')]
        for path in paths:
            frames.append(dict(path=path, sha256=path, declared_dark=level == 0,
                dark_like_not_confirmed=level == 0, folder_iso_matches=True,
                metadata={'exposure_s': .005 if level == 0 else .01},
                black=[100]*4, cfa=[[0, 1], [3, 2]],
                rois=[{'planes': [{'mean_dn': 100 + signal} for _ in offsets]} for _ in range(3)]))
        pairs.append(dict(left=paths[0], right=paths[1], status='candidate',
            both_dark_like_not_confirmed=level == 0, declared_dark_pair=level == 0,
            mixed_capture_roles=False, matching_exposure_metadata=True, matching_geometry=True,
            measurements=[dict(roi=roi, offset=offset, status='candidate_stable',
                signal_mean_dn=100 + signal, noise_variance_dn2=2*signal + 7)
                for roi in range(3) for offset in offsets]))
    return {'groups': [dict(folder='iso-100', frames=frames, pair_candidates=pairs)]}


class PtcFitTests(unittest.TestCase):
    def test_exact_line(self):
        points = [dict(signal_dn=x, variance_dn2=3*x+7, pair_id=str(x)) for x in (10, 30, 100)]
        model = fit(points)
        self.assertAlmostEqual(model['a_dn_per_signal_dn'], 3)
        self.assertAlmostEqual(model['b_dn2'], 7)

    def test_boundary_exposes_negative_intercept(self):
        points = [dict(signal_dn=x, variance_dn2=3*x-7, pair_id=str(x)) for x in (10, 30, 100)]
        self.assertTrue(fit(points)['intercept_at_boundary'])
        self.assertAlmostEqual(fit(points, False)['b_dn2'], -7)

    def test_degenerate_rejected(self):
        with self.assertRaises(ValueError):
            fit([dict(signal_dn=1, variance_dn2=2, pair_id='a')]*3)

    def test_declared_dark_pair_is_reference_not_light_fit_point(self):
        records = analyze(synthetic_audit())
        self.assertEqual(len(records), 4)
        for record in records:
            self.assertEqual(record['independent_pairs'], 3)
            self.assertEqual(record['dark_variance_dn2'], 7)
            self.assertAlmostEqual(record['constrained']['a_dn_per_signal_dn'], 2)
            self.assertFalse(record['dark_identity_confirmed'])

    def test_no_stable_dark_pair_excludes_fit(self):
        audit = synthetic_audit()
        for measurement in audit['groups'][0]['pair_candidates'][0]['measurements']:
            measurement['status'] = 'reject_drift_or_structure'
        self.assertTrue(all(r['status'] == 'excluded_no_stable_declared_dark_pair'
                            for r in analyze(audit)))

    def test_same_pixels_under_different_paths_do_not_count_twice(self):
        audit = synthetic_audit()
        frames = audit['groups'][0]['frames']
        frames[-2]['sha256'] = frames[-4]['sha256']
        self.assertTrue(all(r['independent_pairs'] == 2 for r in analyze(audit)))


if __name__ == '__main__':
    unittest.main()
