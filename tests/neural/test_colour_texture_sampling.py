import unittest

import torch

from colour_texture_sampling import balanced_signs,colour_class,colour_coverage
from training_texture_data import training_texture,training_context,apply_colour_flips
from fine_texture_training import texture
from context_data import crop_centre


class ColourTextureTests(unittest.TestCase):
    def test_sign_balance_covers_all_classes_without_global_rng_changes(self):
        events=[dict(serial=i,family='chromatic_wave',phase=i%4,frequency=8.,amplitude=.06,colours=[1.,-.5,.7]) for i in range(12)]
        before=torch.get_rng_state().clone()
        assigned=balanced_signs(events)
        self.assertEqual(assigned,balanced_signs(events))
        self.assertTrue(torch.equal(before,torch.get_rng_state()))
        changed=[dict(e,colours=[c*s for c,s in zip(e['colours'],assigned[e['serial']])]) for e in events]
        for level in range(3):
            self.assertEqual({colour_class(e['colours']) for e in changed if e['serial']%3==level},{0,1,2,3})
        self.assertLess(colour_coverage(changed)['missing_cells'],colour_coverage(events)['missing_cells'])

    def test_context_matches_centre_and_unchanged_channels_are_bit_exact(self):
        event=dict(serial=7,texture_serial=12000000,family='chromatic_wave',colour_flips=[1,-1,1])
        original,meta=texture(event['family'],event['texture_serial'],128)
        image,adjusted=training_texture(event)
        self.assertTrue(torch.equal(image[:,0],original[:,0]))
        self.assertTrue(torch.equal(image[:,2],original[:,2]))
        self.assertTrue(torch.equal(image[:,1],2*meta['dc']-original[:,1]))
        self.assertEqual(adjusted['dc'],meta['dc'])
        self.assertEqual([abs(v) for v in adjusted['colours']],[abs(v) for v in meta['colours']])
        self.assertTrue(torch.equal(crop_centre(training_context(event,256)),image))
        plain,_=apply_colour_flips(original,meta,[1,1,1])
        self.assertTrue(torch.equal(plain,original))

    def test_invalid_signs_or_degenerate_coefficients_rejected(self):
        image,metadata=texture('chromatic_wave',1,128)
        for flips in ([1,True,1],[1,0,1],[1,1],[1.,1,1]):
            with self.assertRaises(ValueError):
                apply_colour_flips(image,metadata,flips)
        with self.assertRaises(ValueError):
            colour_class([1.,0.,.5])
