"""Corpus candidates never override protected compositions or prove clean truth."""
import unittest

from audit_existing_training_corpus import declared_role,alias_key,candidate_groups,CAMERA


class CorpusAuditTests(unittest.TestCase):
    def record(self,n=1000,folder='pictures/scene',iso=4000,second=1):
        return dict(path=f'{folder}/6V1A{n:04d}.CR3',complete_basic=True,
            metadata=dict(model=CAMERA,iso=iso,exposure_s=.005,aperture=5.6,focal_mm=26.,
                          datetime_original=f'2026:11:01 12:00:{second:02d}'),
            role='unqualified_single_or_sequence')

    def test_every_variant_of_reserved_scene_is_protected(self):
        for n in (301,320,327,352,366):
            self.assertEqual(declared_role(self.record(n,iso=12800)),'reserved_evaluated_control')
        self.assertEqual(declared_role(self.record(180)),'existing_training_composition')
        self.assertEqual(declared_role(self.record(250)),'reserved_unqualified_vegetation')

    def test_same_filename_on_other_camera_is_not_pooled(self):
        r=self.record(320);r['metadata']['model']='Canon EOS 60D'
        self.assertEqual(declared_role(r),'separate_camera_qualification_needed')

    def test_calibration_and_usage_rights_are_not_image_training(self):
        self.assertEqual(declared_role(self.record(folder='local/protocole/blacks')),'calibration_only_candidate')
        self.assertEqual(declared_role(self.record(folder='pictures/samples')),'usage_rights_need_confirmation')

    def test_alias_key_is_a_conservative_frame_candidate_not_path_identity(self):
        a=self.record();b=self.record(folder='D:/DCIM/101EOSR6')
        self.assertEqual(alias_key(a),alias_key(b))
        b['metadata']['datetime_original']='2025:01:01 12:00:01'
        self.assertNotEqual(alias_key(a),alias_key(b))

    def test_sequence_requires_minimum_length_time_settings_and_order(self):
        records=[self.record(n=1000+i,second=i//3) for i in range(9)]
        self.assertEqual(len(candidate_groups(records)),1)
        self.assertEqual(candidate_groups(records[:7]),[])
        records[4]['metadata']['iso']=1600
        self.assertEqual(candidate_groups(records),[])
        records=[self.record(n=1000+i,second=i*6) for i in range(9)]
        self.assertEqual(candidate_groups(records),[])

    def test_protected_frames_cannot_make_candidate_sequences(self):
        records=[self.record(n=301+i,second=i//3) for i in range(9)]
        for r in records:r['role']=declared_role(r)
        self.assertEqual(candidate_groups(records),[])


if __name__=='__main__':unittest.main()
