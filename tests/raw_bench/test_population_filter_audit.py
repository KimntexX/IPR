import copy
import itertools
import unittest
import numpy as np
from audit_population_filter import validate
from study_population_filter import gates, PATTERNS, DOMAINS, CONTROLS


def design():
    samples=np.broadcast_to(np.eye(3),(32,5,3,3)).tolist()
    profile=np.asarray(samples).mean(axis=0).tolist()
    baseline=dict(rgb_mse=1.,opponent_mse=[1.,1.,1.],opponent_bias=[0.,0.,0.],pattern_gain=1.,pixels_sha256='truth')
    filtered=dict(baseline,rgb_mse=.8)
    measured=dict(noisy_100=baseline,clean_100=baseline,external_covariance_noisy=filtered,
                  external_covariance_clean_stress=filtered,zero_model_clean=baseline)
    rows=[dict(iso=iso,domain=domain,distribution=distribution,pattern=pattern,seed=seed,
        input_rgb_mse=1.,input_sha256=dict(truth='truth'),transport=dict(workers=8),
        calibration_covariances={n:samples for n in ('spectral','bank')},
        covariance_profiles={n:profile for n in ('spectral','bank')},
        measurements={n:measured for n in ('spectral','bank')})
        for iso,domain,distribution,pattern,seed in itertools.product((1600,51200),DOMAINS,('gaussian','scaled_poisson'),PATTERNS,CONTROLS)]
    return dict(rows=rows,gates=gates(rows),contracts=dict(parities=256,zero_identities=128),
        verdict='measured_same_filter_population_budget_not_production_validation')


class AuditTests(unittest.TestCase):
    def test_complete_grid_passes(self):validate(design())

    def test_missing_case_or_hidden_gate_fails(self):
        data=design();data['rows'].pop()
        with self.assertRaises(ValueError):validate(data)
        data=design();data['gates']['real_read_replay']['spectral']['passed']=False
        with self.assertRaises(ValueError):validate(data)

    def test_changed_profile_or_zero_identity_fails(self):
        data=copy.deepcopy(design());data['rows'][0]['covariance_profiles']['spectral'][0][0][0]=2.
        with self.assertRaises(ValueError):validate(data)
        data=copy.deepcopy(design());data['rows'][0]['input_sha256']['truth']='wrong'
        with self.assertRaises(ValueError):validate(data)


if __name__=='__main__':unittest.main()
