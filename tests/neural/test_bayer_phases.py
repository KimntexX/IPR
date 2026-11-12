import unittest
import torch

from audit_bayer_phases import phase_diagnostics
from joint_bayer_unet import sample_rgb,bilinear
from synthetic_bayer_data import PATTERNS


class BayerPhaseAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):torch.set_num_threads(2)

    def test_identical_constant_truth_all_phases_has_no_baseline_variance(self):
        target=torch.tensor([-.1,.25,1.2])[None,:,None,None].expand(1,3,128,128).clone()
        bases=[bilinear(sample_rgb(target,torch.tensor([p])),torch.tensor([p])) for p in PATTERNS]
        result=phase_diagnostics(bases,bases,target)
        self.assertLess(result['bilinear_phase_variance'],1e-14)
        self.assertLess(result['output_phase_variance'],1e-14)

    def test_phase_bias_is_not_hidden_by_global_mean(self):
        target=torch.zeros(1,3,128,128);base=[target.clone() for _ in range(4)]
        predictions=[target+value for value in (-.1,.1,-.1,.1)]
        result=phase_diagnostics(predictions,base,target)
        self.assertAlmostEqual(result['output_phase_variance'],.01,places=6)
        self.assertEqual(result['bilinear_phase_variance'],0.)
        self.assertLess(result['output_bias_rgb_per_phase'][0][0],0.)

    def test_bad_phase_coverage_and_nonfinite_are_rejected(self):
        target=torch.zeros(1,3,128,128);rows=[target]*4
        with self.assertRaises(ValueError):phase_diagnostics(rows[:3],rows,target)
        bad=target.clone();bad[0,0,0,0]=float('nan')
        with self.assertRaises(ValueError):phase_diagnostics([bad]*4,rows,target)


if __name__=='__main__':unittest.main()
