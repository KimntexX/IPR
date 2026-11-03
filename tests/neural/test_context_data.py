import unittest

import torch

from context_data import extend_signal, extend_edge, extend_texture, extend_observation, margin
from fine_texture_training import texture
from run_diverse_moment_pilot import edge_recipe, edge_image
from synthetic_bayer_data import signal, FAMILIES, PATTERNS, noisy_observation


class ContextDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)

    def test_analytic_signal_centres_and_signed_ramps_are_exact(self):
        for family in FAMILIES:
            for seed in (704, 3700203):
                expected = signal(family, seed, 128)[None]
                image = extend_signal(family, seed)
                self.assertTrue(torch.equal(image[:, :, 64:-64, 64:-64], expected))
                self.assertTrue(torch.isfinite(image).all())
        with self.assertRaises(ValueError):
            margin(258, 128)

    def test_training_edge_and_texture_centres_are_exact(self):
        for index in (0, 17, 417):
            recipe = edge_recipe(index, 'train')
            self.assertTrue(torch.equal(extend_edge(recipe)[:, :, 64:-64, 64:-64], edge_image(recipe)))
            for family in ('weak_achromatic_wave', 'chromatic_wave'):
                image = extend_texture(family, index)
                self.assertTrue(torch.equal(image[:, :, 64:-64, 64:-64], texture(family, index, 128)[0]))

    def test_halo_noise_is_independent_without_mutating_centres_or_rng(self):
        truth = extend_signal('ramp', 704)
        state = torch.get_rng_state().clone()
        for pattern in PATTERNS:
            tags = torch.tensor([pattern])
            original, sigma = noisy_observation(truth[:, :, 64:-64, 64:-64], tags,
                .0008, .0004, torch.Generator().manual_seed(70))
            before, deviations = original.clone(), sigma.clone()
            expanded, expanded_sigma = extend_observation(truth, tags, original, sigma, .0008, .0004, 170000000)
            different, _ = extend_observation(truth, tags, original, sigma, .0008, .0004, 170000001)
            self.assertTrue(torch.equal(expanded[:, :, 32:-32, 32:-32], original))
            self.assertTrue(torch.equal(expanded_sigma[:, :, 32:-32, 32:-32], sigma))
            self.assertFalse(torch.equal(expanded[:, :, :16], different[:, :, :16]))
            self.assertTrue(torch.equal(original, before))
            self.assertTrue(torch.equal(sigma, deviations))
        self.assertTrue(torch.equal(state, torch.get_rng_state()))

    def test_spectral_and_fine_historical_centres_are_exact(self):
        from audit_spectral_noise import periodic_signal
        from confirm_fine_noise import fine_signal
        from context_data import crop_centre
        for orientation in (0, 1, 2):
            for chromatic in (False, True):
                centre, _, _ = periodic_signal(24, .02, orientation, chromatic)
                extended, _, _ = periodic_signal(24, .02, orientation, chromatic, 384, -96)
                self.assertTrue(torch.equal(centre, crop_centre(extended, 192)))
        case = dict(frequency=18, amplitude=.06, angle=.63, chromatic=True, phase=1.17)
        centre, _, _ = fine_signal(case)
        extended, _, _ = fine_signal(case, 384, -96)
        self.assertTrue(torch.equal(centre, crop_centre(extended, 192)))
