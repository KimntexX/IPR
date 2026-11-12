import unittest
from physical_parameter_uncertainty import cluster_slopes


class PhysicalParameterUncertaintyTests(unittest.TestCase):
    def test_exact_linear_law_survives_pair_cluster_bootstrap(self):
        records=[dict(pair=p,signal_dn=s,variance_dn2=3*s+4) for p,s in ((1,10),(1,20),(2,30),(3,40))]
        result=cluster_slopes(records,4,73)
        self.assertAlmostEqual(result['point_slope_dn'],3)
        for value in result['descriptive_percentile95']:
            self.assertAlmostEqual(value,3)
        self.assertEqual(result['pairs'],[1,2,3])

    def test_single_pair_cannot_fake_multiple_independent_captures(self):
        with self.assertRaises(ValueError):
            cluster_slopes([dict(pair=1,signal_dn=i+1,variance_dn2=3) for i in range(20)],1,73)
