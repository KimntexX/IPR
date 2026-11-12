import unittest
import torch
from dru_audit_math import loss_probe,fixed_blurs,recipe
from dru_training_data import core_loss


class AuditMathTests(unittest.TestCase):
    def test_normalized_residual_gradient_matches_loss_autograd(self):
        target=torch.zeros(1,4,8,8);sigma=torch.full_like(target,.02)
        observed=torch.linspace(-.02,.02,256).reshape_as(target)
        residual=torch.zeros_like(target,requires_grad=True)
        loss=core_loss(observed-sigma*residual,target,sigma,margin=0);loss.backward()
        expected=-2*sigma*observed/(sigma.square().mean(dim=(-2,-1),keepdim=True)*256)
        torch.testing.assert_close(residual.grad,expected)
        witness=loss_probe(observed,target,sigma,0)
        scaled=expected.square().mean().sqrt()*128
        self.assertAlmostEqual(float(scaled),witness['standardized_residual_gradient_rms_without_pixel_factor'],places=6)

    def test_blur_preserves_constant_interior_but_attenuates_oscillation(self):
        constant=torch.ones(1,4,16,16)
        for values in fixed_blurs(constant).values():torch.testing.assert_close(values[:,:,2:-2,2:-2],constant[:,:,2:-2,2:-2])
        oscillation=constant.clone();oscillation[:,:,:,::2]=-1
        blurred=fixed_blurs(oscillation)['1.0']
        self.assertLess(float(blurred[:,:,2:-2,2:-2].square().mean()),float(oscillation.square().mean()))

    def test_phase_and_family_joint_cycle_and_zero_noise_are_explicit(self):
        rows=[recipe(i,71,[{}, {}, {}]) for i in range(32)]
        for family in range(8):self.assertEqual({r['phase_index'] for r in rows if r['serial']%8==family},set(range(4)))
        self.assertIsNone(rows[0]['scenario_index'])
        self.assertTrue(all(r['scenario_index'] is not None for r in rows[1:]))


if __name__=='__main__':unittest.main()
