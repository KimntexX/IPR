import unittest

from study_noise_transfer import fit_model, predict, validate, MODELS


def points():
    return [dict(pair_id=str(i), exposure_group=str(i // 2), capture_day=str(i % 2),
                 roi=roi, signal_dn=float(x), variance_dn2=3*x+7.)
            for i, x in enumerate((10, 30, 60, 100, 200, 400)) for roi in range(3)]


class TransferTests(unittest.TestCase):
    def test_exact_affine_all_models(self):
        for kind in MODELS:
            model = fit_model(points(), kind, 7.)
            self.assertAlmostEqual(predict(model, 50.), 157., places=4)

    def test_wrong_dark_is_not_silently_ignored(self):
        self.assertEqual(fit_model(points(), 'dark_fixed', 20.)['b'], 20.)

    def test_degenerate_and_nonfinite_rejected(self):
        for value in (float('nan'), 0., -1.):
            invalid = points()
            invalid[0]['signal_dn'] = value
            with self.assertRaises(ValueError):
                fit_model(invalid, 'affine', 7.)

    def test_group_excludes_all_matching_exposures(self):
        result = validate(points(), 'affine', 7., 'exposure_group')
        self.assertEqual(len(result['folds']), 3)
        self.assertEqual(result['evaluated_pairs'], 6)
        self.assertLess(result['relative_mae'], 1e-10)
        for fold in result['folds']:
            held_ids = {p['pair_id'] for p in fold['predictions']}
            self.assertEqual(len(held_ids), 2)

    def test_small_training_set_explicitly_excluded(self):
        result = validate(points()[:6], 'affine', 7., 'capture_day')
        self.assertIsNone(result['relative_mae'])
        self.assertTrue(all(f['status'] == 'insufficient_training_pairs' for f in result['folds']))

    def test_roi_duplication_does_not_increase_pair_weight(self):
        original = points()
        original[0]['variance_dn2'] += 20
        extra = original + [dict(p) for p in original if p['pair_id'] == '0']
        for kind in ('affine', 'relative_affine', 'dark_fixed', 'quadratic'):
            self.assertAlmostEqual(fit_model(original, kind, 7.)['a'], fit_model(extra, kind, 7.)['a'])


if __name__ == '__main__':
    unittest.main()
