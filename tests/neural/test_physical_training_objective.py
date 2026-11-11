import unittest
import torch
from physical_training_objective import training_loss,verify_controls,fixed_sigma_scale


class PhysicalTrainingObjectiveTests(unittest.TestCase):
    def test_normalization_equalizes_standardized_errors_and_residual_derivatives(self):
        sigma=torch.tensor([.001,.01],requires_grad=True)
        residual=torch.tensor([.5,.5],requires_grad=True)
        target=torch.zeros(2)
        prediction=sigma.detach()*residual
        value=training_loss(prediction,target,sigma,'sigma_normalized_l1',1e-5)
        self.assertAlmostEqual(float(value.detach()),.5,places=6)
        value.backward()
        torch.testing.assert_close(residual.grad,torch.tensor([.5,.5]))
        self.assertIsNone(sigma.grad)

    def test_floor_keeps_zero_sigma_finite_without_clipping_signed_errors(self):
        prediction=torch.tensor([-.01,.02],requires_grad=True)
        value=training_loss(prediction,torch.zeros(2),torch.tensor([0.,.01]),'sigma_normalized_l1',.001)
        self.assertAlmostEqual(float(value.detach()),6,places=5)
        value.backward()
        self.assertTrue(torch.isfinite(prediction.grad).all())

    def test_current_objective_stays_bit_exact_and_bad_inputs_refused(self):
        x=torch.tensor([-.1,.3]);target=torch.zeros(2);sigma=torch.ones(2)
        self.assertTrue(torch.equal(training_loss(x,target,sigma,'unweighted L1',.001),torch.nn.functional.l1_loss(x,target)))
        for invalid in (torch.tensor([float('nan'),1.]),torch.tensor([-1.,1.])):
            with self.assertRaises(ValueError):training_loss(x,target,invalid,'sigma_normalized_l1',.001)
        with self.assertRaises(ValueError):training_loss(x,target,sigma,'unknown',.001)

    def test_parent_control_identity_and_values_must_match_before_training(self):
        parent=[dict(target_index=0,iso=3200,corrected_mse=.01,observed_mse=.02)]
        self.assertEqual(verify_controls(parent,parent),0)
        with self.assertRaises(ValueError):verify_controls([{**parent[0],'corrected_mse':.02}],parent)
        with self.assertRaises(ValueError):verify_controls([{**parent[0],'iso':6400}],parent)

    def test_fixed_scale_preserves_local_weight_ratios_and_scales_gradients(self):
        sigma=torch.tensor([.002,.02]);target=torch.zeros(2)
        a=torch.tensor([.1,.1],requires_grad=True)
        raw=training_loss(a,target,sigma,'sigma_normalized_l1',1e-5);raw.backward()
        b=a.detach().clone().requires_grad_()
        scaled=training_loss(b,target,sigma,'sigma_normalized_l1',1e-5,.01);scaled.backward()
        torch.testing.assert_close(scaled,raw*.01)
        torch.testing.assert_close(b.grad,a.grad*.01)
        self.assertAlmostEqual(float(b.grad[0]/b.grad[1]),10.,delta=2e-6)
        with self.assertRaises(ValueError):training_loss(b,target,sigma,'unweighted L1',1e-5,.01)
        with self.assertRaises(ValueError):training_loss(b,target,sigma,'sigma_normalized_l1',1e-5,0.)

    def test_training_harmonic_mean_makes_calibration_average_weight_one(self):
        sigma=torch.tensor([.002,.004,.01],dtype=torch.float64)
        scale=fixed_sigma_scale(float(sigma.reciprocal().sum()),3)
        self.assertAlmostEqual(float((scale/sigma).mean()),1.)
        for total,count in ((float('nan'),3),(0.,3),(100.,0),(100.,True)):
            with self.assertRaises(ValueError):fixed_sigma_scale(total,count)

    def test_mean_seeking_objectives(self):
        target=torch.zeros(2,4,8,8)
        sigma=torch.full_like(target,.01)
        shifted=torch.full_like(target,.1,requires_grad=True)
        self.assertAlmostEqual(float(training_loss(shifted,target,sigma,'unweighted L2',1e-5)),.01,places=7)
        self.assertAlmostEqual(float(training_loss(shifted,target,sigma,'l1_plane_mean',1e-5)),.2,places=6)
        zero_mean=torch.tensor([.1,-.1]*256).reshape(2,4,8,8)
        self.assertAlmostEqual(float(training_loss(zero_mean,target,sigma,'l1_plane_mean',1e-5)),.1,places=6)
        for objective in ('unweighted L2','l1_plane_mean'):
            with self.assertRaises(ValueError):training_loss(shifted,target,sigma,objective,1e-5,.5)
