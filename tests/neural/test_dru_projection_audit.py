import unittest
import torch
from audit_dru_projection import cancellation_witness,projection_rows
from dru_structure_objective import structure_penalty


class ProjectionAuditTests(unittest.TestCase):
    def test_explicit_null_direction_changes_fine_component_without_penalty(self):
        result=cancellation_witness()
        self.assertAlmostEqual(result['coefficient_changes'][-1],-.2)
        self.assertGreater(result['raw_mse'],0.)
        self.assertLess(result['loss'],1e-20)
        self.assertLess(result['float32_loss'],1e-10)

    def test_scale_reconstruction_matches_actual_objective(self):
        torch.manual_seed(19)
        target=torch.rand(1,4,32,32,dtype=torch.float64)
        # Repeat a single plane so fixture helper spatial and plane means coincide.
        target=target[:,:1].expand_as(target).clone();prediction=.8*target
        rows=projection_rows(prediction,target)
        expected=sum(r['signed_projection']**2 for r in rows)/3
        self.assertAlmostEqual(float(structure_penalty(prediction,target,0)),expected,places=12)


if __name__=='__main__':unittest.main()
