import unittest
import numpy as np
from prepare_native_normalized_cfa import native_patch


class NativeNormalizedTests(unittest.TestCase):
    def test_invalid_divisor_or_black_cannot_create_nonfinite_targets(self):
        for black,divisor in (([512]*4,float('nan')),([512]*3,13483),([float('inf')]*4,13483)):
            with self.assertRaises(ValueError):native_patch(np.zeros((8,8),dtype=np.uint16),[[0,1],[3,2]],black,divisor)

    def test_native_common_divisor_and_signed_values_all_phases(self):
        pattern=np.array([[0,1],[3,2]])
        levels=np.array([500,800,1000,900],dtype=np.uint16)
        for y in (False,True):
            for x in (False,True):
                labels=pattern[::-1 if y else 1,::-1 if x else 1]
                result,physical=native_patch(np.tile(levels[labels],(4,4)),labels,[512,513,514,515],13483)
                for i,colour in enumerate(physical):
                    expected=np.float32(int(levels[colour])-[512,513,514,515][colour])*np.float32(1/13483)
                    np.testing.assert_array_equal(result[i],expected)
                self.assertTrue((result[0]<0).all())


if __name__=='__main__':unittest.main()
