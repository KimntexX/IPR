import json
import unittest
from unittest.mock import patch
import numpy as np
from study_spectral_budget import gates,DOMAINS,CALIBRATION,CONTROL,GUIDE
from study_spatial_budget import ensemble,ROIS,PATTERNS


def scores():
    rows=[]
    for domain in DOMAINS:
        for pattern in PATTERNS:
            models={label:dict(comparison=[dict(relative_frobenius_error=error,sigma_predicted_over_observed=[1.,1.,1.],relative_mc_standard_error=.01)]*5)
                    for label,error in (('flat',.1),('spatial',.05),('clean_diagnostic',.03))}
            rows.append(dict(domain=domain,pattern=pattern,regions={'edge':dict(models=models)}))
    return rows


class SpectralBudgetContracts(unittest.TestCase):
    def test_real_domain_cannot_be_hidden_by_internal_pass(self):
        rows=scores();result=gates(rows)
        self.assertTrue(all(v['passed'] for v in result.values()))
        rows[-1]['regions']['edge']['models']['spatial']['comparison'][0]['sigma_predicted_over_observed']=[1.5,1.,1.]
        result=gates(rows)
        self.assertTrue(result['spectral_internal']['passed'])
        self.assertFalse(result['real_read_replay']['passed'])
        json.dumps(result,allow_nan=False)

    def test_population_seeds_new_and_disjoint(self):
        self.assertFalse(set(CALIBRATION)&set(CONTROL));self.assertNotIn(GUIDE,CALIBRATION+CONTROL)
        self.assertGreater(min(CALIBRATION),90032)

    def test_read_provider_drawn_once_per_realization_and_bounded_batches(self):
        calls=[];seeds=tuple(range(32));nominal=np.zeros((256,256))
        def provider(seed):calls.append(seed);return np.full((256,256),seed/1000.)
        def transport(tool,inputs,env):
            self.assertLessEqual(len(inputs),8)
            return [(np.repeat(v[...,None],3,axis=2),{}) for v in inputs]
        def covariances(residual,rois):return {name:np.zeros((5,3,3)).tolist() for name in rois}
        with patch('study_spatial_budget.native_batch',side_effect=transport),patch('study_spatial_budget.simulate_context',side_effect=lambda c,n,r,g,d:r),patch('study_spatial_budget.covariance_fields',side_effect=covariances):
            result=ensemble(None,nominal,provider,dict(iso=1600),seeds,'gaussian',{})
        self.assertEqual(calls,list(seeds));self.assertEqual(len(result['identities']),32)


if __name__=='__main__':unittest.main()
