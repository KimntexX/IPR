import unittest
import numpy as np
from audit_native_cfa_bridge import sensor_crop, compare


class NativeBridgeTests(unittest.TestCase):
    def test_all_orientations_map_to_expected_sensor_positions(self):
        raw=np.arange(120,dtype=np.uint16).reshape(10,12)
        for orientation in range(8):
            crop=sensor_crop(raw,[1,2],2,orientation)
            for y in range(2):
                for x in range(2):
                    sx,sy=1+x,2+y
                    if orientation&4:sx,sy=sy,sx
                    if orientation&1:sx=11-sx
                    if orientation&2:sy=9-sy
                    self.assertEqual(crop[y,x],raw[sy,sx])

    def test_signed_float_bridge_recovers_integer_dn(self):
        dn=np.array([[500,1000],[512,15000]],dtype=np.uint16)
        native=(dn.astype(np.float32)-512)*np.float32(1/13483)
        result=compare(native,dn,[[0,1],[3,2]],[512]*4,13483)
        self.assertEqual(result['integer_dn_mismatches'],0)
        self.assertEqual(result['negative_fraction'],.25)


if __name__=='__main__':unittest.main()
