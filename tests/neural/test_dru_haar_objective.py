import unittest
import torch
from dru_haar_objective import haar_step,local_haar_penalty,noise_aware_haar_penalty


class HaarObjectiveTests(unittest.TestCase):
    def test_orthonormal_step_preserves_energy_and_constant_details_vanish(self):
        x=torch.randn(1,4,64,64,dtype=torch.float64,generator=torch.Generator().manual_seed(27))
        low,bands=haar_step(x)
        self.assertAlmostEqual(float(x.square().sum()),float(low.square().sum()+sum(b.square().sum() for b in bands)),places=10)
        _,bands=haar_step(torch.ones_like(x))
        self.assertTrue(all(torch.count_nonzero(b)==0 for b in bands))

    def test_zero_exact_and_uniform_targets_and_finite_backward(self):
        x=torch.randn(1,4,64,64,generator=torch.Generator().manual_seed(28),requires_grad=True)
        self.assertEqual(float(local_haar_penalty(x,x,0).detach()),0.)
        value=local_haar_penalty(x,torch.ones_like(x),0)
        self.assertEqual(float(value.detach()),0.);value.backward()
        self.assertTrue(torch.isfinite(x.grad).all())

    def test_gain_gradient_corrects_attenuation(self):
        x=torch.randn(1,4,64,64,generator=torch.Generator().manual_seed(29))*.02
        gain=torch.tensor(.8,requires_grad=True)
        value=local_haar_penalty(gain*x,x,0);value.backward()
        self.assertAlmostEqual(float(value.detach()),.04,places=4)
        self.assertLess(float(gain.grad),0.)

    def test_invalid_grid_and_mismatched_targets_are_rejected(self):
        x=torch.zeros(1,4,64,64)
        with self.assertRaises(ValueError):local_haar_penalty(x,x,4)
        with self.assertRaises(ValueError):local_haar_penalty(x,x[:,:,:32,:],0)

    def test_zero_noise_recovers_the_local_penalty_and_gradient_is_finite(self):
        target=torch.randn(1,4,64,64,generator=torch.Generator().manual_seed(30))*.02
        prediction=(.8*target).requires_grad_()
        current=noise_aware_haar_penalty(prediction,target,torch.zeros_like(target),0)
        torch.testing.assert_close(current,local_haar_penalty(prediction,target,0))
        current.backward();self.assertTrue(torch.isfinite(prediction.grad).all())
        with self.assertRaises(ValueError):noise_aware_haar_penalty(prediction,target,torch.full_like(target,-1.),0)

    def test_expected_noise_term_does_not_explode_for_a_weak_target(self):
        generator=torch.Generator().manual_seed(31)
        target=torch.randn(32,4,64,64,generator=generator)*.001
        sigma=torch.full_like(target,.02)
        prediction=target+torch.randn(target.shape,generator=generator)*sigma
        value=float(noise_aware_haar_penalty(prediction,target,sigma,0))
        self.assertGreater(value,.8);self.assertLess(value,1.2)


if __name__=='__main__':unittest.main()
