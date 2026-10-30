import copy
import unittest
from audit_local_budget import validate
from study_local_budget import gates,ROUTES
from test_population_filter_audit import design as parent_design


def design():
    parent=parent_design();rows=copy.deepcopy(parent['rows'])
    for row in rows:
        for measured in row['measurements'].values():
            for route in ROUTES:
                measured[f'external_{route}_noisy']=copy.deepcopy(measured['external_covariance_noisy'])
                measured[f'external_{route}_clean_stress']=copy.deepcopy(measured['external_covariance_clean_stress'])
    return dict(rows=rows,parities=640,gates=gates(rows),verdict='measured_local_routing_ablation_not_production_validation'),parent


class LocalAuditTests(unittest.TestCase):
    def test_paired_grid_passes(self):
        data,parent=design();validate(data,parent)

    def test_input_or_baseline_change_fails(self):
        data,parent=design();data['rows'][0]['input_sha256']['truth']='different'
        with self.assertRaises(ValueError):validate(data,parent)
        data,parent=design();data['rows'][0]['measurements']['bank']['noisy_100']['rgb_mse']=2.
        with self.assertRaises(ValueError):validate(data,parent)

    def test_missing_parity_and_hidden_failure_fail(self):
        data,parent=design();data['parities']=639
        with self.assertRaises(ValueError):validate(data,parent)
        data,parent=design();data['gates']['cap']['real_read_replay']['spectral']['passed']=False
        with self.assertRaises(ValueError):validate(data,parent)


if __name__=='__main__':unittest.main()
