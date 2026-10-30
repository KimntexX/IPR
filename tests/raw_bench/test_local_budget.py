import copy
import unittest
from study_local_budget import gates,ROUTES
from study_population_filter import DOMAINS


def design():
    baseline=dict(rgb_mse=1.,opponent_mse=[1.,1.,1.],opponent_bias=[0.,0.,0.],pattern_gain=1.)
    candidate=dict(baseline,rgb_mse=.8)
    rows=[]
    for domain in DOMAINS:
        models={}
        for name in ('spectral','bank'):
            model=dict(noisy_100=copy.deepcopy(baseline),clean_100=copy.deepcopy(baseline))
            for route in ROUTES:
                model[f'external_{route}_noisy']=copy.deepcopy(candidate)
                model[f'external_{route}_clean_stress']=copy.deepcopy(candidate)
            models[name]=model
        rows.append(dict(domain=domain,pattern='weak_color',input_rgb_mse=1.,measurements=models))
    return rows


class LocalBudgetTests(unittest.TestCase):
    def test_routes_and_domains_stay_separate(self):
        rows=design();result=gates(rows)
        self.assertTrue(all(result[r][d][n]['passed'] for r in ROUTES for d in DOMAINS for n in ('spectral','bank')))
        rows[1]['measurements']['bank']['external_cap_noisy']['pattern_gain']=.95
        result=gates(rows)
        self.assertFalse(result['cap']['real_read_replay']['bank']['passed'])
        self.assertTrue(result['fallback']['real_read_replay']['bank']['passed'])
        self.assertTrue(result['cap']['spectral_internal']['bank']['passed'])

    def test_clean_stress_veto_not_dropped(self):
        rows=design();rows[0]['measurements']['spectral']['external_fallback_clean_stress']['pattern_gain']=.5
        self.assertFalse(gates(rows)['fallback']['spectral_internal']['spectral']['passed'])


if __name__=='__main__':unittest.main()
