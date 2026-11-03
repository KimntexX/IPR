import unittest

import torch

from audit_spectral_noise import periodic_signal, response


class SpectralNoiseTests(unittest.TestCase):
    def test_response_recovers_each_channel_attenuation_phase_and_offset(self):
        truth, wave, cosine = periodic_signal(16, .02, 2, True)
        expected = torch.tensor((.8, 1.1, .9))
        altered = .3+expected[None, :, None, None]*(truth-.3)+.004
        measured = response(altered, truth, wave, cosine)
        torch.testing.assert_close(torch.tensor(measured['sine_gain']), expected, atol=2e-6, rtol=0)
        torch.testing.assert_close(torch.tensor(measured['dc_error']), torch.full((3,), .004), atol=2e-7, rtol=0)
        self.assertLess(max(map(abs, measured['quadrature_gain'])), 2e-6)
        self.assertGreater(measured['rgb_mse'], 0.)

    def test_exact_truth_and_rejection(self):
        truth, wave, cosine = periodic_signal(4, .005, 0, False)
        measured = response(truth, truth, wave, cosine)
        self.assertEqual(measured['rgb_mse'], 0.)
        self.assertEqual(measured['sine_gain'], [1., 1., 1.])
        with self.assertRaises(ValueError):
            periodic_signal(64, .005, 0, False)
        with self.assertRaises(ValueError):
            response(truth*float('nan'), truth, wave, cosine)


if __name__ == '__main__':
    unittest.main()
