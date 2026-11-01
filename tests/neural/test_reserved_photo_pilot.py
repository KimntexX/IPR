"""Composition contracts and immutable control identities, without photo access."""
from copy import deepcopy
import unittest

from qualify_photo_pairs import overlap_protocol
from run_reserved_photo_pilot import select_inputs,validation_identities,coverage


class ReservedPilotTests(unittest.TestCase):
    def parents(self):
        def record(index,scene):
            return dict(scene=scene,source_sha256=f'{index:064x}',model='camera',
                profile_sha256='p',manifest_sha256='m',domain='signed camera',calibration_applied=True)
        old=dict(extraction=dict(records=[record(i,'indoor') for i in range(27)]+[record(100,'vegetation')]),
                 freeze=dict(qualification=dict(ecc_floor=.97,max_translation_pixels=12.)))
        control=dict(status='primary_control_pairs_available',
            freeze=dict(reference_group=1,qualification=overlap_protocol(old['freeze']['qualification'])),
            captures=[record(i,'control_indoor') for i in range(30,51)])
        return old,control

    def test_only_training_composition_and_reserved_primary_selected(self):
        old,control=self.parents();before=deepcopy((old,control))
        captures,roles,_=select_inputs(old,control)
        self.assertEqual(len(captures),48)
        self.assertEqual(roles,dict(indoor='train',control_indoor='validation'))
        self.assertNotIn('vegetation',{c['scene'] for c in captures})
        self.assertEqual((old,control),before)

    def test_diagnostic_reference_cannot_replace_primary(self):
        old,control=self.parents();control['freeze']['reference_group']=3
        with self.assertRaisesRegex(ValueError,'diagnostic'):select_inputs(old,control)

    def test_raw_identity_cannot_cross_partitions(self):
        old,control=self.parents()
        control['captures'][0]['source_sha256']=old['extraction']['records'][0]['source_sha256']
        with self.assertRaisesRegex(ValueError,'Duplicate'):select_inputs(old,control)

    def test_profile_calibration_or_protocol_mismatch_fails(self):
        for field in ('profile_sha256','manifest_sha256','domain'):
            old,control=self.parents();control['captures'][0][field]='other'
            with self.assertRaisesRegex(ValueError,'contract'):select_inputs(old,control)
        old,control=self.parents();control['freeze']['qualification']['ecc_floor']=.9
        with self.assertRaisesRegex(ValueError,'protocol'):select_inputs(old,control)
        old,control=self.parents();control['captures'][0]['calibration_applied']=False
        with self.assertRaisesRegex(ValueError,'Calibration'):select_inputs(old,control)

    def test_validation_identity_ignores_training_but_not_target_changes(self):
        validation=dict(id='v',split='validation',source_sha256='s',content_sha256='c',target_sha256='t')
        pairs=dict(examples=[validation,dict(id='train',split='train')])
        self.assertEqual(validation_identities(pairs),[('v','s','c','t')])
        validation['target_sha256']='changed'
        self.assertNotEqual(validation_identities(pairs),[('v','s','c','t')])

    def test_iso_coverage_does_not_invent_shared_levels(self):
        result=coverage([dict(split='train',iso=12800),dict(split='validation',iso=1600)])
        self.assertEqual(result,dict(train={'12800':1},validation={'1600':1},test={}))


if __name__=='__main__':unittest.main()
