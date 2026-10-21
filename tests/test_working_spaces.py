import unittest
import numpy as np
from compare_working_spaces import spaces, operate, current_gamut, encode16, rgb_matrix, PRIMARIES, WHITES


class SpaceTests(unittest.TestCase):
    def test_white_and_neutral_axis_preserved(self):
        for definition in spaces().values():
            np.testing.assert_allclose(definition['to_srgb']@np.ones(3),np.ones(3),atol=2e-7)
            self.assertAlmostEqual(float(definition['Y_weights'].sum()),1,places=6)

    def test_signed_extended_round_trip_and_invariant_operators(self):
        source=np.random.default_rng(1337).uniform(-.3,2,(8,8,3)).astype(np.float32)
        definitions=spaces()
        for operation in ('neutral','exposure_minus1EV','exposure_plus1EV','chroma_1p25_commonY'):
            reference=operate(source,operation,definitions['srgb_extended']['Y_weights'])
            for definition in definitions.values():
                working=source@definition['from_srgb'].T
                returned=operate(working,operation,definition['Y_weights'])@definition['to_srgb'].T
                np.testing.assert_allclose(returned,reference,rtol=0,atol=5e-6)

    def test_rgb_curve_depends_on_primaries(self):
        source=np.array([[[.8,.1,.02]]],dtype=np.float32);definitions=spaces()
        a=operate(source,'signed_channel_power_1p2',definitions['srgb_extended']['Y_weights'])
        d=definitions['prophoto_linear']
        b=operate(source@d['from_srgb'].T,'signed_channel_power_1p2',d['Y_weights'])@d['to_srgb'].T
        self.assertGreater(float(np.max(np.abs(a-b))),.01)

    def test_nominal_cube_clipping_is_lossy(self):
        source=np.array([[[1.2,-.1,.3]]],dtype=np.float32)
        clipped=operate(source,'clamp_unit_cube',np.ones(3))
        self.assertFalse(np.array_equal(source,clipped))
        np.testing.assert_array_equal(clipped,np.array([[[1,0,.3]]],dtype=np.float32))

    def test_neutral_output_and_high_bit_precision(self):
        source=np.array([[[0,0,0],[1,1,1],[.18,.18,.18]]],dtype=np.float32)
        mapped=current_gamut(source);np.testing.assert_allclose(mapped,source,atol=1e-7)
        encoded=encode16(mapped)
        self.assertEqual(encoded.dtype,np.uint16)
        np.testing.assert_array_equal(encoded[0,:2],np.array([[0,0,0],[65535,65535,65535]],dtype=np.uint16))

    def test_matrices_use_correct_white(self):
        matrix=rgb_matrix(PRIMARIES['rec2020_linear'],WHITES['rec2020_linear'])
        self.assertAlmostEqual(float(matrix[1].sum()),1,places=12)
        self.assertAlmostEqual(float(matrix[1,0]),.2627,places=3)


if __name__=='__main__':unittest.main()
