import unittest
from copy import deepcopy
from qualify_photo_pairs import scene_radiometry
from run_enriched_photo_pilot import combine,example_identities


class EnrichedPilotTests(unittest.TestCase):
    def fixture(self):
        capture=dict(source_sha256='a',scene='indoor',model='R6',profile_sha256='p',
            manifest_sha256='m',domain='camera',calibration_applied=True)
        other=dict(capture,source_sha256='b',scene='burst_4000_training_candidate')
        return [capture],dict(indoor='train'),dict(status='candidate_training_pairs_available',captures=[other])

    def test_combination_preserves_scene_and_capture_identity(self):
        c,r,b=self.fixture();before=deepcopy((c,r,b))
        combined,roles=combine(c,r,b)
        self.assertEqual(len(combined),2)
        self.assertEqual(roles['burst_4000_training_candidate'],'train')
        self.assertEqual((c,r,b),before)

    def test_cross_scene_duplicate_rejected(self):
        c,r,b=self.fixture();b['captures'][0]['source_sha256']='a'
        with self.assertRaises(ValueError):combine(c,r,b)

    def test_different_profile_or_unqualified_burst_rejected(self):
        c,r,b=self.fixture();b['captures'][0]['profile_sha256']='other'
        with self.assertRaises(ValueError):combine(c,r,b)
        c,r,b=self.fixture();b['status']='incomplete'
        with self.assertRaises(ValueError):combine(c,r,b)

    def test_per_scene_policy_and_unknown_policy(self):
        f=dict(scene_radiometry=dict(burst='identity_same_iso'))
        self.assertEqual(scene_radiometry(f,'burst'),'identity_same_iso')
        self.assertEqual(scene_radiometry(f,'indoor'),'global_channel_affine')
        with self.assertRaises(ValueError):scene_radiometry(dict(radiometry_policy='unknown'),'indoor')

    def test_target_changes_are_detectable(self):
        row=dict(id='p',source_sha256='s',content_sha256='x',target_sha256='y')
        self.assertNotEqual(example_identities([row]),example_identities([dict(row,target_sha256='z')]))


if __name__=='__main__':unittest.main()
