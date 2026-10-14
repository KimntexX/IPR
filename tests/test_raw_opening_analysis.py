"""Opening summary guardrails; no private corpus."""
import unittest
from summarize_raw_opening import summarize


def fixture():
    stats = dict(output=dict(mean_Y=.2),flower_ROI=dict(G_over_R_of_means=.5),encoded_clipped_percent=0)
    record = dict(scene='S',sha256='source',status='measured',sample_replay_max_abs_float_error=0,
                  reference_kelvin=4924,stages={k:stats for k in ('current','hue_preserving_curve','display_5200_tint5_diagnostic')})
    inventory = dict(RAW=[dict(scene='S',sha256='source')],neutral_exports=[])
    return [dict(record=record)],inventory


class OpeningTests(unittest.TestCase):
    def test_missing_reference_remains_explicit(self):
        result = summarize(*fixture())
        self.assertEqual(len(result['missing_LR']),1)
        self.assertFalse(result['decision']['production_modified'])
        self.assertFalse(result['decision']['photographic_validation'])

    def test_source_hash_mismatch_refused(self):
        probes,inventory=fixture()
        probes[0]['record']['sha256']='other'
        with self.assertRaisesRegex(ValueError,'source hash mismatch'):
            summarize(probes,inventory)

    def test_witness_error_refused(self):
        probes,inventory=fixture()
        probes[0]['record']['sample_replay_max_abs_float_error']=.01
        with self.assertRaisesRegex(ValueError,'Failed RAW'):
            summarize(probes,inventory)

    def test_missing_or_duplicated_RAW_refused(self):
        probes,inventory=fixture()
        for values in ([],probes*2):
            with self.assertRaisesRegex(ValueError,'Missing or duplicate'):
                summarize(values,inventory)

    def test_camera_neutral_reference_only(self):
        probes,inventory=fixture()
        def row(profile):
            return dict(scene='S',file='reference',status='measured',
                        metadata=dict(sha256='ref',xmp_settings=dict(CameraProfile=profile,WhiteBalance='Custom',Temperature='5200',Tint='+5')),
                        distribution=dict(whole_region=dict(mean_Y=.3)))
        inventory['neutral_exports']=[row('Adobe Standard')]
        self.assertFalse(summarize(probes,inventory)['comparisons'])
        inventory['neutral_exports']=[row('Camera Neutral')]
        result=summarize(probes,inventory)
        self.assertEqual(len(result['comparisons']),1)
        self.assertFalse(result['comparisons'][0]['WB_equivalence_verified'])
        self.assertEqual(result['comparisons'][0]['LR_WB'],'Custom')


if __name__=='__main__':
    unittest.main()
