import copy
import unittest
import numpy as np
from signed_measurement import REQUIRED_STAGES, measure_pair, moments, output_effect, pair_moments, planes, safe_ratio


class SignedMeasurementTest(unittest.TestCase):
    def test_pair_variance_and_drift_are_distinct(self):
        x = np.arange(16).reshape(4, 4, 1)
        result = pair_moments(x, x + 3)
        self.assertEqual(result['difference_over_sqrt2']['variance'], [0.])
        self.assertAlmostEqual(result['difference_over_sqrt2']['raw_second_moment'][0], 4.5)
        self.assertEqual(result['first_minus_second_mean'], [-3.])

    def test_covariance_keeps_channel_correlation(self):
        x = np.arange(16).reshape(4, 4, 1)
        data = np.concatenate((x, 2*x, -x), axis=-1)
        cov = np.asarray(moments(data)['covariance'])
        self.assertAlmostEqual(cov[0, 1], 2*cov[0, 0])
        self.assertAlmostEqual(cov[0, 2], -cov[0, 0])

    def test_cfa_greens_not_merged(self):
        d = dict(layout='cfa', origin_yx=[0, 0], context=dict(cfa=[[0, 1], [3, 2]]))
        result = planes(d, np.arange(64).reshape(8, 8, 1))
        self.assertEqual([v[0] for v in result],
                         ['phase00_colour0', 'phase01_colour1', 'phase10_colour3', 'phase11_colour2'])
        self.assertEqual([v[1].shape for v in result], [(4, 4, 1)]*4)

    def test_zero_ratio_is_null_not_infinite(self):
        self.assertEqual(safe_ratio([1, 2], [0, 4]), [None, .5])

    def test_nonfinite_rejected(self):
        with self.assertRaises(ValueError):
            moments(np.full((2, 2, 1), np.nan))

    def fixture(self):
        d = dict(shape=[4, 4, 1], dtype='f32', layout='cfa', origin_yx=[0, 0],
                 orientation='sensor', unit='DN', context=dict(cfa=[[0, 1], [3, 2]]))
        data = {}
        for policy in ('parity', 'signed'):
            data[policy] = []
            for i in range(2):
                stages = {}
                for stage in REQUIRED_STAGES:
                    desc = copy.deepcopy(d)
                    rgb = 'rgb' in stage
                    if rgb:
                        desc.update(layout='rgb', shape=[4, 4, 3])
                    if stage not in ('unpacked_cfa', 'black_subtracted_cfa'):
                        desc['unit'] = 'linear_camera_white_1'
                    stages[(stage, (0, 0))] = (desc, np.full(desc['shape'], i, dtype='<f4'))
                data[policy].append((dict(mode=policy, source=dict(sha256=str(i))), stages))
        return data

    def test_input_identity_is_enforced(self):
        data = self.fixture()
        self.assertEqual(len(measure_pair(data)), 20)
        data['signed'][0][1][('unpacked_cfa', (0, 0))][1][0, 0, 0] = 5
        with self.assertRaises(ValueError):
            measure_pair(data)

    def test_missing_stage_rejected(self):
        data = self.fixture()
        data['signed'][0][1].clear()
        with self.assertRaises(ValueError):
            measure_pair(data)

    def test_pre_output_stage_required(self):
        data = self.fixture()
        for pair in data.values():
            for capture in pair:
                del capture[1][('pre_output_camera_rgb', (0, 0))]
        with self.assertRaises(ValueError):
            measure_pair(data)

    def test_output_bound_mean_shift(self):
        before = np.array([-2, -1, 0, 1], dtype='<f4').reshape(2, 2, 1)
        result = output_effect(before, np.clip(before, 0, 1), 'signed')
        self.assertEqual(result['mean_shift'], [.75])
        self.assertEqual(result['lower_clipped_fraction'], [.5])

    def test_output_bound_identity_required(self):
        before = np.zeros((2, 2, 1), dtype='<f4')
        with self.assertRaises(ValueError):
            output_effect(before, before+1, 'signed')

    def test_source_mismatch_rejected(self):
        data = self.fixture()
        data['signed'][0][0]['source']['sha256'] = 'other'
        with self.assertRaises(ValueError):
            measure_pair(data)


if __name__ == '__main__':
    unittest.main()
