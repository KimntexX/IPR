"""Global/local geometry, interpolation support and refusal contracts."""
import unittest
import cv2
import numpy as np

from audit_pair_geometry import fit_geometry,local_transform,transformed_reference


class GeometryTests(unittest.TestCase):
    def test_local_coordinates_preserve_global_mapping(self):
        angle=.013
        full=np.array([[np.cos(angle),-np.sin(angle),93.],
                       [np.sin(angle),np.cos(angle),-15.]])
        origin=np.array([1800.,2400.]);point=np.array([125.,211.])
        local=local_transform(full,origin)
        np.testing.assert_allclose(local[:,:2]@point+local[:,2],
            full[:,:2]@(point+origin)+full[:,2]-origin,atol=1e-5)

    def test_translation_larger_than_old_gate_has_supported_overlap(self):
        reference=np.ones((384,384,3),np.float32)
        original=reference.copy();mask=np.ones((384,384),bool)
        target,support=transformed_reference(reference,mask,np.array([[1,0,130],[0,1,-15]],np.float32))
        self.assertFalse(support[:,0:130].any())
        self.assertFalse(support[-15:,:].any())
        self.assertTrue(support[50:250,160:350].all())
        np.testing.assert_array_equal(target[support],np.ones((support.sum(),3),np.float32))
        np.testing.assert_array_equal(reference,original)

    def test_invalid_neighbor_is_not_accepted_at_interpolation_boundary(self):
        image=np.ones((64,64,3),np.float32);mask=np.ones((64,64),bool);mask[:,10]=False
        _,support=transformed_reference(image,mask,np.array([[1,0,.5],[0,1,0]],np.float32))
        self.assertFalse(support[20,10])
        self.assertFalse(support[20,11])

    def test_proxy_estimate_keeps_full_resolution_units(self):
        rng=np.random.default_rng(919)
        reference=cv2.GaussianBlur(rng.uniform(.05,.5,(180,240)).astype(np.float32),(0,0),2.)
        source=cv2.warpAffine(reference,np.array([[1,0,4],[0,1,-2]],np.float32),(240,180))
        r=fit_geometry(reference,source,[720,960,3],'translation')
        self.assertEqual(r['status'],'measured')
        np.testing.assert_allclose(r['center_shift_xy'],[16,-8],atol=.3)
        self.assertGreater(r['maximum_corner_shift_pixels'],12.)

    def test_flat_nonfinite_and_unknown_models_do_not_invent_geometry(self):
        flat=np.ones((64,64),np.float32)*.2
        self.assertEqual(fit_geometry(flat,flat,[256,256,3],'rigid')['status'],'unobservable_flat_registration')
        for model in ('unknown',):
            with self.assertRaises(ValueError): fit_geometry(flat,flat,[256,256,3],model)
        broken=flat.copy();broken[1,1]=np.nan
        with self.assertRaises(ValueError):fit_geometry(flat,broken,[256,256,3],'translation')
        with self.assertRaises(ValueError):local_transform([[1,0,float('inf')],[0,1,0]],[0,0])

    def test_rigid_fit_undoes_proxy_crop_origin(self):
        rng=np.random.default_rng(44)
        reference=cv2.GaussianBlur(rng.uniform(.05,.5,(180,240)).astype(np.float32),(0,0),2.)
        matrix=cv2.getRotationMatrix2D((120,90),.8,1.)
        matrix[:,2]+=[2.,-1.]
        source=cv2.warpAffine(reference,matrix,(240,180))
        result=fit_geometry(reference,source,[180,240,3],'rigid')
        self.assertEqual(result['status'],'measured')
        np.testing.assert_allclose(result['reference_to_source_full'],matrix,atol=.12)


if __name__=='__main__':unittest.main()
