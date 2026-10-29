import unittest
from signal_noise_survey import fitted, summarize, predict_variance


def record(index, train, sigma=1., rejected=False):
    return dict(roi=[index, 0, 64, 64], training_original_5x5=train,
                signal_I_median=float(index), quadrant_signal_drift=0.,
                levels=[dict(sigma_I_Co_Cg=[sigma, 2*sigma, 3*sigma], coherent_rejected=rejected) for _ in range(5)])


class SignalSurveyTests(unittest.TestCase):
    def test_empty_witnesses_abstain(self):
        self.assertIsNone(fitted([record(1, True, rejected=True)], 0)['sigma'])

    def test_holdout_does_not_fit_the_model(self):
        records = [record(i, True) for i in range(25)] + [record(i, False, 1000.) for i in range(56)]
        summary = summarize(records)
        self.assertEqual(summary['global_models'][0]['sigma'], [1., 2., 3.])
        self.assertEqual(sum(b['held_count'] for b in summary['bands']), 56)
        self.assertEqual(summary['bands'][0]['conditional_models'][0]['sigma'], [1., 2., 3.])

    def test_signal_bands_do_not_mix_noise_levels(self):
        records = [record(i, True, 1. if i<8 else (2. if i<16 else 4.)) for i in range(25)]
        records += [record(i, False) for i in range(56)]
        summary = summarize(records)
        self.assertEqual([b['conditional_models'][0]['sigma'][0] for b in summary['bands']], [1.,2.,4.])

    def test_split_is_checked(self):
        with self.assertRaises(ValueError):
            summarize([record(1, True)])

    def test_interpolation_is_of_variance(self):
        self.assertAlmostEqual(predict_variance([(0,[1.]),(1,[3.])],.5)[0],5**.5)

    def test_prediction_does_not_extrapolate(self):
        self.assertIsNone(predict_variance([(0,[1.]),(1,[3.])],1.1))
        self.assertIsNone(predict_variance([(0,[1.])],0))


if __name__ == '__main__':
    unittest.main()
