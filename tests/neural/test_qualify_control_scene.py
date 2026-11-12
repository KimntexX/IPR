"""Frozen selection and composition separation; no real photos required."""
from copy import deepcopy
import unittest

from qualify_control_scene import GROUPS,selected_sources,reference_variant
from qualify_photo_pairs import declared_scenes


class ControlSceneTests(unittest.TestCase):
    def records(self):
        return [dict(destination=f'corpus/6V1A{n:04d}.CR3') for n in range(301,367)]

    def captures(self):
        return [dict(group=g,iso=iso,source_sha256=str(n),scene='control_indoor')
                for g,iso,first,last in GROUPS for n in
                (range(first,last+1) if iso==100 else sorted({first,(first+last)//2,last}))]

    def test_selection_is_29_unique_sources(self):
        selected=selected_sources(self.records())
        self.assertEqual(len(selected),29)
        self.assertEqual(len({r['source']['destination'] for r in selected}),29)
        self.assertEqual(sum(r['iso']==100 for r in selected),17)
        self.assertEqual([r['source']['destination'] for r in selected if r['iso']==3200],
                         ['corpus/6V1A0328.CR3','corpus/6V1A0333.CR3','corpus/6V1A0339.CR3'])

    def test_missing_fixed_frame_fails(self):
        with self.assertRaisesRegex(ValueError,'Missing fixed'):
            selected_sources(self.records()[1:])

    def test_duplicate_names_fail(self):
        records=self.records()
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            selected_sources(records+[records[0]])

    def test_references_are_not_pooled(self):
        captures=self.captures();before=deepcopy(captures)
        early=reference_variant(captures,1);late=reference_variant(captures,3)
        self.assertEqual(len(early),21);self.assertEqual(len(late),20)
        self.assertEqual({c['group'] for c in early if c['iso']==100},{1})
        self.assertEqual({c['group'] for c in late if c['iso']==100},{3})
        self.assertEqual(captures,before)

    def test_missing_reference_cannot_silently_drop(self):
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            reference_variant(self.captures()[1:],1)

    def test_scene_identity_comes_from_frozen_partition(self):
        freeze=dict(scene_partition=dict(control_indoor='validation',test=None))
        self.assertEqual(declared_scenes(freeze,self.captures()),['control_indoor'])

    def test_unknown_scene_or_partition_fails(self):
        with self.assertRaisesRegex(ValueError,'partition'):
            declared_scenes(dict(scene_partition=dict(indoor='train')),self.captures())
        with self.assertRaisesRegex(ValueError,'Unknown'):
            declared_scenes(dict(scene_partition=dict(control_indoor='maybe')),[])

    def test_source_duplication_is_not_independence(self):
        captures=self.captures()
        with self.assertRaisesRegex(ValueError,'Duplicate'):
            declared_scenes(dict(scene_partition=dict(control_indoor='validation')),captures+[captures[0]])


if __name__=='__main__': unittest.main()
