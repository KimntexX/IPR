import copy
import unittest
import numpy as np
from study_spatial_budget import fixture,choose_controls,covariance_fields,compare_draws,LEVELS,ROIS,CALIBRATION_SEEDS,CONTROL_SEEDS,GUIDE_SEED
from camera_noise_budget import band_covariances
from transport_batch import native_batch


class SpatialBudgetContracts(unittest.TestCase):
    def test_fresh_seed_sets_disjoint(self):
        self.assertEqual(len(CALIBRATION_SEEDS),32);self.assertEqual(len(CONTROL_SEEDS),32)
        self.assertFalse(set(CALIBRATION_SEEDS)&set(CONTROL_SEEDS))
        self.assertNotIn(GUIDE_SEED,CALIBRATION_SEEDS+CONTROL_SEEDS)
        self.assertTrue(min(CONTROL_SEEDS)>2053)

    def test_single_pyramid_exact_covariance_for_all_regions(self):
        rgb=np.random.default_rng(82001).normal(size=(256,256,3))*.001
        actual=covariance_fields(rgb,ROIS)
        for name,roi in ROIS.items():np.testing.assert_array_equal(actual[name],band_covariances(rgb,roi))

    def test_per_draw_uncertainty_and_identical_mean(self):
        samples=np.array([np.eye(3)*(i+1) for i in range(32)])[:,None,:,:]*np.ones((1,5,1,1))
        result=compare_draws(samples,samples)
        self.assertEqual(len(result),5)
        self.assertEqual(result[0]['relative_frobenius_error'],0)
        self.assertGreater(result[0]['relative_mc_standard_error'],0)
        with self.assertRaises(ValueError):compare_draws(samples[:3],samples[:3])

    def test_no_holdout_or_previous_transport_frames_selected(self):
        inventory=dict(records=[]);selection=[dict(captures=[dict(source=dict(sha256='used'))])]
        for iso in LEVELS:
            for index,split in enumerate(('development','development','final_validation','development')):
                inventory['records'].append(dict(category='confirmed_dark',split=split,metadata=dict(iso=iso,model='Canon EOS R6 Mark III'),
                    sha256='used' if index==0 else f'{iso}-{index}',status='available_hash_match',original_path=f'{index}.CR3'))
        controls=choose_controls(inventory,selection)
        self.assertTrue(all(v['split']=='development' and v['sha256']!='used' for g in controls.values() for v in g))
        bad=copy.deepcopy(inventory);bad['records']=[r for r in bad['records'] if r['split']!='development']
        with self.assertRaises(ValueError):choose_controls(bad,selection)

    def test_batch_size_guards_without_spawning(self):
        for values in ([],[np.zeros((256,256))]*9):
            with self.assertRaises(ValueError):native_batch(None,values,{})

    def test_new_fixtures_positive_finite(self):
        for iso in LEVELS:
            for name in ('flat','diagonal_luma','curved_ramp','diagonal_color'):
                rgb=fixture(iso,name)
                self.assertEqual(rgb.shape,(256,256,3));self.assertTrue(np.isfinite(rgb).all());self.assertGreater(rgb.min(),0)


if __name__=='__main__':unittest.main()
