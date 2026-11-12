import unittest
from qualify_sensor_curve import eligible_curve


class CurveEligibilityTests(unittest.TestCase):
    def test_only_complete_matching_non_preflight_curve_is_accepted(self):
        proof=dict(status='completed_sensor_curve_continuation_not_quality_qualification',seed=71,preflight=False,
            completed_steps=2048,config=dict(steps=2048,scope='cfa_front'),frozen_reconstruction_bit_exact=True,
            losses=[0.]*2048,exposure_counts={'known':8192})
        self.assertTrue(eligible_curve(proof,71));self.assertFalse(eligible_curve(proof,73))
        for change in (dict(preflight=True),dict(completed_steps=576),dict(frozen_reconstruction_bit_exact=False),dict(exposure_counts={'known':8191}),dict(status='stopped_on_error')):
            self.assertFalse(eligible_curve(dict(proof,**change),71))


if __name__=='__main__':unittest.main()
