import unittest
from diagnose_physical_variance_law import fit,cross_validate


class PhysicalVarianceLawTests(unittest.TestCase):
    def records(self):
        return [dict(pair=i,exposure_s=s/1000,signal=s,read=4.,variance=4+3*s-.001*s*s)
                for i,s in enumerate((10,30,100,300))]

    def test_known_quadratic_recovers_coefficients_and_unseen_exposures(self):
        law=fit(self.records(),2)
        self.assertAlmostEqual(law['a'],3)
        self.assertAlmostEqual(law['c'],-.001)
        self.assertLess(cross_validate(self.records(),2)['median_exposure_error'],1e-10)
        self.assertGreater(cross_validate(self.records(),1)['median_exposure_error'],.01)

    def test_duplicate_rois_do_not_increase_a_capture_pair_weight(self):
        rows=self.records();expected=fit(rows,2)
        rows.extend([rows[0]]*20)
        actual=fit(rows,2)
        self.assertAlmostEqual(expected['a'],actual['a'])
        self.assertAlmostEqual(expected['c'],actual['c'])

    def test_invalid_moments_are_refused(self):
        rows=self.records();rows[0]['variance']=float('nan')
        with self.assertRaises(ValueError):fit(rows,2)
