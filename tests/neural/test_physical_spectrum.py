import unittest
import tempfile
from pathlib import Path
import numpy as np
from qualify_physical_spectrum import simulate, aggregate, expanded_inputs
from prepare_next_training import digest


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

    def test_expanded_input_keeps_only_physical_eligible_heldout_and_fixed_gain(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'dark.npy';np.save(path,np.ones((9,4,32,32),dtype=np.uint16))
            dataset=dict(status='completed_expanded_physical_tiles_not_qualified',
                dark_groups=[dict(iso=100,sources=[dict(path=str(path),sha256=digest(path))])],
                light_frames={},fields=[dict(iso=100,plane=0,pair=i,roi=0,split=role,
                    gate=dict(eligible=eligible,signal_dn=80))
                    for i,role,eligible in ((1,'training',True),(2,'heldout',True),(3,'heldout',False))])
            bank=dict(frames=[],fits=[dict(iso=100,plane=0,fit=dict(shot_slope_dn=.2,heldout=[]))])
            sources,fits=expanded_inputs(bank,dataset)
            self.assertEqual(sources[100].shape,(1,9,4,32,32))
            self.assertEqual(fits[(100,0)]['shot_slope_dn'],.2)
            self.assertEqual([f['pair'] for f in fits[(100,0)]['heldout']],[2])
            dataset['dark_groups'][0]['sources'][0]['sha256']='changed'
            with self.assertRaises(ValueError):
                expanded_inputs(bank,dataset)

    def test_incomplete_expansion_is_refused(self):
        with self.assertRaises(ValueError):
            expanded_inputs({},dict(status='stopped'))

    def test_simulation_refuses_reused_or_out_of_bounds_read_frame(self):
        read=np.ones((2,32,32))
        for indices in ((np.array([0,1]),np.array([0,0])),(np.array([0,2]),np.array([1,0]))):
            with self.assertRaises(ValueError):
                simulate(read,10,1,0,0,1,indices)


if __name__=='__main__':
    unittest.main()
