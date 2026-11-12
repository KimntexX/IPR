import unittest

import torch

from paired_structure_loss import coherent_projection, paired_structure_loss
from study_paired_structure import decompose


class PairedStructureTests(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(2)
        x = torch.arange(16,dtype=torch.float32)
        self.clean = (.2+.03*x.sin())[None,None,None,:].expand(1,4,16,16).clone()
        self.sigma = torch.full_like(self.clean,.02)

    def test_constant_and_zero_variance_have_zero_loss_with_finite_gradient(self):
        value = self.clean.clone().requires_grad_()
        loss = paired_structure_loss(value,value,torch.full_like(value,.2),self.sigma,0,'paired_cross')
        self.assertEqual(float(loss),0.)
        loss.backward()
        self.assertTrue(torch.isfinite(value.grad).all())
        self.assertEqual(float(coherent_projection(value,self.clean,torch.zeros_like(value),0).abs().max()),0.)

    def test_cross_is_signed_symmetric_and_has_two_gradients(self):
        first = (self.clean+.01*self.clean).requires_grad_()
        second = (self.clean-.01*self.clean).requires_grad_()
        cross = paired_structure_loss(first,second,self.clean,self.sigma,0,'paired_cross')
        square = paired_structure_loss(first,second,self.clean,self.sigma,0,'paired_square')
        self.assertLess(float(cross),0.)
        self.assertGreater(float(square),0.)
        self.assertTrue(torch.equal(cross,paired_structure_loss(second,first,self.clean,self.sigma,0,'paired_cross')))
        cross.backward()
        self.assertGreater(float(first.grad.abs().sum()),0.)
        self.assertGreater(float(second.grad.abs().sum()),0.)

    def test_independent_draws_remove_projection_variance_but_reused_draw_does_not(self):
        torch.manual_seed(763)
        truth = self.clean.expand(8192,-1,-1,-1)
        sigma = self.sigma.expand_as(truth)
        first = truth+torch.randn_like(truth)*sigma
        second = truth+torch.randn_like(truth)*sigma
        square = paired_structure_loss(first,second,truth,sigma,0,'paired_square')
        cross = paired_structure_loss(first,second,truth,sigma,0,'paired_cross')
        reused = paired_structure_loss(first,first,truth,sigma,0,'paired_cross')
        self.assertAlmostEqual(float(square),1.,delta=.04)
        self.assertAlmostEqual(float(cross),0.,delta=.04)
        self.assertAlmostEqual(float(reused),1.,delta=.04)

    def test_invalid_contracts_rejected(self):
        for value in (-1,True,8):
            with self.assertRaises(ValueError):
                coherent_projection(self.clean,self.clean,self.sigma,value)
        with self.assertRaises(ValueError):
            paired_structure_loss(self.clean,self.clean,self.clean,self.sigma,0,'unknown')

    def test_all_pairs_equal_finite_sample_bias_and_do_not_clip_negative(self):
        q = torch.tensor([[1.,2.],[-1.,3.],[0.,4.]])
        result = decompose(q)
        self.assertLess(result['bias_squared_estimate'][0],0.)
        for i in range(2):
            explicit = sum(float(q[a,i]*q[b,i]) for a in range(3) for b in range(3) if a != b)/6
            self.assertAlmostEqual(result['all_pairs_cross'][i],explicit)
            self.assertAlmostEqual(result['bias_squared_estimate'][i],explicit)
            self.assertAlmostEqual(result['mean_square'][i],result['bias_squared_estimate'][i]+result['sample_variance'][i])
