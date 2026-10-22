import unittest
import numpy as np
from verify_profile_rendering_integration import compare_codes


class ProfileRenderingIntegrationTests(unittest.TestCase):
    def test_r6_requires_exact_identity(self):
        a = np.array([[[10000, 20000, 30000]]], np.uint16)
        self.assertTrue(compare_codes(a, a, exact=True)['passed'])
        self.assertFalse(compare_codes(a, a+1, exact=True)['passed'])

    def test_candidate_has_small_explicit_numeric_tolerance(self):
        a = np.array([[[10000, 20000, 30000]]], np.uint16)
        self.assertTrue(compare_codes(a, a+4)['passed'])
        self.assertFalse(compare_codes(a, a+5)['passed'])

    def test_geometry_mismatch_stops(self):
        with self.assertRaisesRegex(ValueError, 'geometry'):
            compare_codes(np.zeros((2, 2, 3), np.uint16), np.zeros((3, 2, 3), np.uint16))

    def test_subtraction_does_not_wrap_uint16(self):
        a = np.array([0, 65535], np.uint16)
        self.assertEqual(compare_codes(a, a[::-1])['max_16bit_codes'], 65535)


if __name__ == '__main__':
    unittest.main()
