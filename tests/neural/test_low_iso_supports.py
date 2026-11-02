import unittest
import numpy as np

from qualify_low_iso_supports import patch_diagnostics,selected_entries


class LowIsoSupportsTests(unittest.TestCase):
    def test_signed_values_preserved(self):
        image=np.full((96,96,3),-.01,np.float32);before=image.copy()
        row=patch_diagnostics(image)
        self.assertTrue(row['usable_content_support'])
        self.assertEqual(row['negative_fraction_rgb'],[1.,1.,1.])
        np.testing.assert_array_equal(image,before)

    def test_saturation_does_not_become_clean(self):
        row=patch_diagnostics(np.ones((96,96,3),np.float32))
        self.assertFalse(row['usable_content_support'])
        self.assertEqual(row['valid_fraction'],0.)

    def test_invalid_shapes_and_nonfinite_rejected(self):
        for shape in ((32,32,3),(96,96,1)):
            with self.assertRaises(ValueError):patch_diagnostics(np.ones(shape,np.float32))
        image=np.zeros((96,96,3),np.float32);image[0,0,0]=np.nan
        with self.assertRaises(ValueError):patch_diagnostics(image)

    def test_texture_changes_proxy_not_admission(self):
        rng=np.random.default_rng(42)
        image=rng.uniform(.1,.2,(96,96,3)).astype(np.float32)
        self.assertTrue(patch_diagnostics(image)['usable_content_support'])

    def test_selection_rejects_duplicates_and_changed_iso(self):
        rows=[dict(path=str(i),source_sha256=str(i),metadata=dict(iso=100,model='Canon EOS R6 Mark III')) for i in range(5)]
        self.assertEqual(len(selected_entries(dict(screened=rows))),5)
        rows[-1]['source_sha256']='0'
        with self.assertRaises(ValueError):selected_entries(dict(screened=rows))
        rows[-1]['source_sha256']='4';rows[-1]['metadata']['iso']=200
        with self.assertRaises(ValueError):selected_entries(dict(screened=rows))


if __name__=='__main__':unittest.main()
