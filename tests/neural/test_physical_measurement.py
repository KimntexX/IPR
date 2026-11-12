import unittest
import numpy as np
from physical_measurement import vectors,simultaneous,classify,binomial_upper_tail


class PhysicalMeasurementTests(unittest.TestCase):
    def test_signed_uncertainty_accepts_zero_and_rejects_twenty_percent_variance(self):
        rng=np.random.default_rng(101)
        cal=rng.normal(0,[.003,.001,.001,.008],size=(128,8,4))
        test=np.zeros((8,4));report=simultaneous(cal,test)
        self.assertEqual(report['status'],'equivalent_within_declared_bounds')
        test[:,0]=np.log(1.2)
        self.assertEqual(classify(test,report),'outside_bounds')

    def test_large_uncertainty_is_inconclusive_not_qualified(self):
        cal=np.random.default_rng(102).normal(0,.2,size=(128,4,4))
        self.assertEqual(simultaneous(cal,np.zeros((4,4)))['status'],'inconclusive')

    def test_noise_vectors_have_theoretical_variance_and_no_nan_on_invalid_input(self):
        x=np.random.default_rng(103).normal(0,2,(32,64,64))
        actual=vectors(x)
        self.assertAlmostEqual(float(np.exp(actual[:,0]).mean()),2,delta=.1)
        with self.assertRaises(ValueError):vectors(np.zeros((32,32)))

    def test_counting_tail_has_known_limits(self):
        self.assertAlmostEqual(binomial_upper_tail(0,10),1)
        self.assertAlmostEqual(binomial_upper_tail(10,10),.05**10)

    def test_six_joint_metrics_use_their_own_bounds(self):
        cal=np.random.default_rng(104).normal(0,.001,size=(64,5,6))
        report=simultaneous(cal,np.zeros((5,6)),bounds=np.full(6,.02),names=[str(i) for i in range(6)])
        self.assertEqual(report['status'],'equivalent_within_declared_bounds')
        self.assertEqual(classify(np.full((5,6),.06),report),'outside_bounds')
        with self.assertRaises(ValueError):classify(np.full((5,6),np.nan),report)
        with self.assertRaises(ValueError):simultaneous(cal,np.zeros((5,6)),bounds=np.ones(5))


if __name__=='__main__':unittest.main()
