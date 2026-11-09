import unittest
import torch
from physical_training_objective import training_loss,verify_controls


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
