import unittest
from analyse_dru_amplitudes import summarize


class AmplitudeAnalysisTests(unittest.TestCase):
    def row(self):
        return dict(arm='reference',seed=71,step=128,amplitude=.008,frequency=26,chromatic=False,noise_index=1,
            site_colours=[3,2,0,1],corrected=[dict(sine_gain=.8,mse=1.) for _ in range(4)],
            observed=[dict(sine_gain=.9,mse=2.) for _ in range(4)])

    def test_gain_change_is_paired_difference_and_mse_ratio_is_weighted(self):
        a=self.row();b=self.row();b['observed']=[dict(sine_gain=1.1,mse=8.) for _ in range(4)]
        output=summarize([a,b])
        self.assertAlmostEqual(output[0]['mean_gain_change_from_observed'],-.2)
        self.assertAlmostEqual(output[0]['mse_ratio'],.2)

    def test_physical_colour_order_and_invalid_values(self):
        row=self.row();row['corrected'][2]['sine_gain']=.4
        self.assertEqual(summarize([row])[0]['mean_gain'],.4)
        row['observed'][0]['mse']=0.
        with self.assertRaises(ValueError):summarize([row])


if __name__=='__main__':unittest.main()
