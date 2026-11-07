import unittest
import torch
from dru_structure_objective import structure_penalty,loss_components
from dru_training_data import core_loss


class StructureObjectiveTests(unittest.TestCase):
    def fixture(self):
        x=torch.arange(32,dtype=torch.float32)
        return (.15+.02*torch.sin(x*1.1))[None,None,None,:].expand(1,4,32,32).clone()

    def test_reference_loss_and_gradient_match_current_mse_exactly(self):
        target=self.fixture();p=(target+.003).requires_grad_();sigma=torch.full_like(p,.02)
        old=core_loss(p,target,sigma,0);new,_,penalty=loss_components(p,target,sigma,'mse',0)
        self.assertTrue(torch.equal(old,new));self.assertEqual(float(penalty),0.)
        a=torch.autograd.grad(old,p)[0];b=torch.autograd.grad(new,p)[0]
        self.assertTrue(torch.equal(a,b))

    def test_exact_target_and_flat_truth_have_no_projection_penalty(self):
        target=self.fixture()
        self.assertEqual(float(structure_penalty(target,target,0)),0.)
        self.assertEqual(float(structure_penalty(target,torch.ones_like(target),0)),0.)

    def test_attenuation_penalty_and_gradient_push_toward_clean_contrast(self):
        target=self.fixture();scale=torch.tensor(.8,requires_grad=True)
        value=structure_penalty(.15+scale*(target-.15),target,0);value.backward()
        self.assertAlmostEqual(float(value.detach()),.04,places=3)
        self.assertLess(float(scale.grad),0.)

    def test_finite_flat_gradient_and_invalid_core(self):
        p=torch.ones(1,4,16,16,requires_grad=True)
        v=structure_penalty(p,torch.zeros_like(p),0);v.backward()
        self.assertTrue(torch.isfinite(p.grad).all())
        with self.assertRaises(ValueError):structure_penalty(p,p,8)


if __name__=='__main__':unittest.main()
