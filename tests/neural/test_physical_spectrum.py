import unittest
import numpy as np
from qualify_physical_spectrum import simulate, aggregate


class PhysicalSpectrumTests(unittest.TestCase):
    def test_capture_pairs_have_equal_weight_despite_different_roi_counts(self):
        rows=[dict(iso=100,plane=0,pair=1,value=[1.,2.,3.,4.]) for _ in range(8)]
        rows.append(dict(iso=100,plane=0,pair=2,value=[3.,4.,5.,6.]))
        values,identities=aggregate(rows,'value')
        np.testing.assert_array_equal(values,[[2.,3.,4.,5.]])
        self.assertEqual(identities[0]['pairs'],[1,2])

    def test_replicate_axis_preserved_when_grouping(self):
        rows=[dict(iso=iso,plane=0,pair=iso,value=np.full((64,4),iso)) for iso in (100,3200)]
        values,_=aggregate(rows,'value')
        self.assertEqual(values.shape,(64,2,4))
        np.testing.assert_array_equal(values[:,1,:],3200)

    def test_shared_frame_indices_keep_empirical_plane_covariance(self):
        rng=np.random.default_rng(123)
        read=rng.normal(size=(8,64,64))
        indices=(np.arange(128)%8,(np.arange(128)+1)%8)
        a=simulate(read,0,1,0,0,21,indices)
        b=simulate(read*2,0,1,0,0,99,indices)
        np.testing.assert_array_equal(b,a*2)
        np.testing.assert_array_equal(a,simulate(read,0,1,0,0,21,indices))


if __name__=='__main__':
    unittest.main()
