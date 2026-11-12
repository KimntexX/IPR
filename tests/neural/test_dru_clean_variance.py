import unittest
import torch
from dru_clean_variance import clean_sigma


class CleanVarianceTests(unittest.TestCase):
    def scenario(self):
        return dict(parameters=[dict(colour=c,a=.01*(c+1),b=.0001,gain=2.,divisor=1.) for c in range(4)])

    def test_scaling_colour_mapping_and_negative_signal(self):
        labels=torch.tensor([[3,2,0,1]])
        target=torch.full((1,4,2,2),.15);target[:,:,0,0]=-.1
        sigma=clean_sigma(target,labels,self.scenario())
        for site,colour in enumerate(labels[0].tolist()):
            self.assertAlmostEqual(float(sigma[0,site,1,1]**2),.01*(colour+1)*.15+.0001,places=8)
            self.assertAlmostEqual(float(sigma[0,site,0,0]**2),.0001,places=8)

    def test_zero_noise_and_bad_coefficients(self):
        labels=torch.tensor([[0,1,3,2]]);target=torch.ones(1,4,2,2)
        self.assertTrue(torch.equal(clean_sigma(target,labels,None),torch.zeros_like(target)))
        invalid=self.scenario();invalid['parameters'][0]['divisor']=0.
        with self.assertRaises(ValueError):clean_sigma(target,labels,invalid)
        with self.assertRaises(ValueError):clean_sigma(target,torch.tensor([[0,0,1,2]]),self.scenario())


if __name__=='__main__':unittest.main()
