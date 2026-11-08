import unittest
from prepare_larger_physical_tiles import enlarge,physical_gate


class LargerPhysicalTileTests(unittest.TestCase):
    def test_enlargement_keeps_centres_and_does_not_mutate_old_rois(self):
        old=[dict(x=600,y=400,width=128,height=128)]
        self.assertEqual(enlarge(old),[dict(x=536,y=336,width=256,height=256)])
        self.assertEqual(old[0]['x'],600)
        with self.assertRaises(ValueError):
            enlarge(old,255)

    def test_expanded_region_must_still_be_stable_uniform_and_unsaturated(self):
        self.assertTrue(physical_gate(1512,5,30,2000,512)['eligible'])
        self.assertFalse(physical_gate(1512,20,30,2000,512)['eligible'])
        self.assertFalse(physical_gate(1512,5,80,2000,512)['eligible'])
        self.assertFalse(physical_gate(1512,5,30,12000,512)['eligible'])
        self.assertFalse(physical_gate(500,0,0,600,512)['eligible'])
