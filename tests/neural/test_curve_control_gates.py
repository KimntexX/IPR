import unittest
from run_curve_controls import development_passed


class CurveControlGatesTests(unittest.TestCase):
    def test_neither_partial_seed_nor_top_level_flag_can_unlock_regressions(self):
        proof=dict(status='completed_fixed_edge_protection_probe_not_photographic_qualification',pilot_gates_passed=True,
            models=[dict(seed=71,pilot_gates_passed=True),dict(seed=73,pilot_gates_passed=True)])
        self.assertTrue(development_passed(proof))
        self.assertFalse(development_passed(dict(proof,models=proof['models'][:1])))
        self.assertFalse(development_passed(dict(proof,pilot_gates_passed=False)))
        self.assertFalse(development_passed(dict(proof,status='incomplete')))
        self.assertFalse(development_passed(dict(proof,models=[proof['models'][0],dict(seed=73,pilot_gates_passed=False)])))


if __name__=='__main__':unittest.main()
