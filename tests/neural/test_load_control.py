import unittest
from load_control import cpu_fractions
from scheduled_comparison_results import strict,promising


class LoadPolicyTests(unittest.TestCase):
    def test_external_cpu_excludes_own_process_and_skips_nonmonotonic_counters(self):
        value=cpu_fractions(dict(total=100,idle=10,process=5),dict(total=200,idle=30,process=35))
        self.assertAlmostEqual(value['total_cpu_fraction'],.8);self.assertAlmostEqual(value['external_cpu_fraction'],.5)
        self.assertIsNone(cpu_fractions(dict(total=100,idle=10,process=5),dict(total=100,idle=10,process=5)))
        self.assertIsNone(cpu_fractions(dict(total=100,idle=10,process=5),dict(total=200,idle=5,process=35)))

    def test_long_training_requires_signal_bounds_and_real_noise_benefit(self):
        value=dict(minimum_gain=.98,maximum_gain=1.02,maximum_mse_ratio=.99,median_mse_ratio=.88,groups_below095=0)
        self.assertTrue(strict(value))
        for field,bad in (('minimum_gain',.94),('maximum_gain',1.06),('maximum_mse_ratio',1.01),('median_mse_ratio',.99)):
            self.assertFalse(strict(dict(value,**{field:bad})))

    def test_exploration_requires_paired_nonregression_and_no_inversion(self):
        reference=dict(groups_below095=20,minimum_gain=.5,median_mse_ratio=1.)
        self.assertTrue(promising(dict(reference,groups_below095=18),reference))
        self.assertFalse(promising(dict(reference,minimum_gain=-.1,median_mse_ratio=.8),reference))
        self.assertFalse(promising(reference,reference))


if __name__=='__main__':unittest.main()
