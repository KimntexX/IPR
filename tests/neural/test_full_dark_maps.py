import unittest
import numpy as np
from full_dark_maps import update_moments,summarize_maps


class FullDarkMapTests(unittest.TestCase):
    def test_stream_float32_matches_offline_reference_with_sub_DN_error(self):
        for black,sigma in ((512,1),(2048,60)):
            data=np.random.default_rng(80).normal(black,sigma,(55,32,32)).round().astype(np.uint16)
            mean=np.zeros((32,32),np.float32);m2=np.zeros_like(mean)
            for i,value in enumerate(data,1):update_moments(mean,m2,value,i)
            np.testing.assert_allclose(mean,data.mean(axis=0),atol=.002,rtol=0)
            np.testing.assert_allclose(m2/54,data.var(axis=0,ddof=1),atol=.01,rtol=.001)

    def test_constant_and_hot_means_retain_physical_coordinates(self):
        a=np.full((32,40),512,np.float32);b=a.copy();v=np.ones_like(a)
        a[3,7]=600;b[3,7]=601
        rows,mask=summarize_maps(a,v,b,v,32,20)
        self.assertEqual(sum(r['atypical_mean_candidates'] for r in rows),1)
        self.assertEqual(sum(r['replicated_candidates'] for r in rows),1)
        self.assertTrue(mask[3,7]);self.assertEqual(mask.sum(),1)
        self.assertEqual(sum(r['pixels'] for r in rows),32*40)


if __name__=='__main__':unittest.main()
