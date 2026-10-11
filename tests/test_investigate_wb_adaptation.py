import unittest
import numpy as np
from investigate_wb_adaptation import adaptation, encode_gamut


class AdaptationHypothesisTests(unittest.TestCase):
    def test_all_hypotheses_are_identity_at_reference(self):
        for model in ('current_projection', 'without_cold_diagonal', 'full_Bradford'):
            np.testing.assert_allclose(adaptation(5200, 5200, model), np.eye(3), atol=1e-14)

    def test_full_bradford_is_invertible_and_cold_ablation_is_local(self):
        forward = adaptation(2000, 5200, 'full_Bradford')
        inverse = adaptation(5200, 2000, 'full_Bradford')
        np.testing.assert_allclose(inverse @ forward, np.eye(3), atol=1e-13)
        np.testing.assert_allclose(adaptation(9000, 5200, 'current_projection'),
                                   adaptation(9000, 5200, 'without_cold_diagonal'))
        self.assertFalse(np.allclose(adaptation(2000, 5200, 'current_projection'), forward))

    def test_gamut_projection_is_finite_bounded_and_preserves_in_gamut_grey(self):
        values = np.array([[[.18, .18, .18], [-1., 2., 5.], [0., 0., 0.]]])
        result = encode_gamut(values)
        self.assertTrue(np.isfinite(result).all())
        self.assertTrue(((result >= 0) & (result <= 1)).all())
        np.testing.assert_array_equal(result[0, 0], np.full(3, result[0, 0, 0]))
        np.testing.assert_array_equal(result[0, 2], np.zeros(3))


if __name__ == '__main__':
    unittest.main()
