import unittest
from diagnose_dru_controls import aggregate


class DiagnosisTests(unittest.TestCase):
    def setUp(self):
        self.case=dict(frequency=6,amplitude=.06,chromatic=False,angle=.17)
        self.row=dict(case_index=0,noise_index=1,site_colours=[3,2,0,1],**{k:self.case[k] for k in ('frequency','chromatic','angle')},
            corrected=[dict(sine_gain=g,quadrature_gain=0.,dc_error=0.,mse=m) for g,m in zip([.3,.2,1.,.1],[3.,2.,1.,4.])],
            observed=[dict(mse=2.) for _ in range(4)])

    def test_physical_colour_mapping_and_mse_ratio(self):
        rows=aggregate([self.row],[self.case])
        self.assertEqual([r['mean_gain'] for r in rows],[1.,.1,.2,.3])
        self.assertEqual([r['mse_ratio'] for r in rows],[.5,2.,1.,1.5])

    def test_rejects_repeated_sites_and_nonfinite_values(self):
        with self.assertRaises(ValueError):aggregate([dict(self.row,site_colours=[0,0,1,2])],[self.case])
        self.row['corrected'][0]['sine_gain']=float('nan')
        with self.assertRaises(ValueError):aggregate([self.row],[self.case])

    def test_ratio_uses_sum_of_errors_instead_of_mean_of_ratios(self):
        other=dict(self.row,observed=[dict(mse=8.) for _ in range(4)])
        self.assertAlmostEqual(aggregate([self.row,other],[self.case])[0]['mse_ratio'],.2)


if __name__=='__main__':unittest.main()
