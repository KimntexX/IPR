import unittest
from audit_signal_filter import validate,aggregate
from study_signal_filter import PATTERNS,CONTROL_SEEDS,VARIANTS
from study_cfa_signal_transport import LEVELS


def complete():
    rows=[]
    for iso,levels in LEVELS.items():
        for level in levels:
            for pattern in PATTERNS:
                for shot in range(2):
                    for seed in CONTROL_SEEDS:
                        metrics={key:dict(rgb_mse=1.,opponent_mse=[1.,1.,1.],opponent_bias=[0.,0.,0.]) for key in VARIANTS.values()}
                        metrics['workers']=8
                        rows.append(dict(iso=iso,level=level,pattern=pattern,shot=shot,seed=seed,input_rgb_mse=2.,measurements=metrics))
    return dict(rows=rows,verdict='measured_same_filter_signal_profiles_not_production_validation',
                contracts=dict(pixel_parities=160,zero_identities=80))


class AuditContracts(unittest.TestCase):
    def test_full_design_and_axis_aggregation(self):
        data=complete()
        self.assertEqual(len(validate(data)),20)
        group=aggregate(data['rows'])[0]['variants']['signal_profile']
        self.assertEqual(group['cases'],8)
        self.assertEqual(group['strictly_better_cases'],0)
        self.assertEqual(group['median_opponent_mse_ratio'],[1.,1.,1.])

    def test_missing_and_duplicate_cases_rejected(self):
        for duplicate in (False,True):
            data=complete()
            if duplicate:data['rows'][-1]=data['rows'][0]
            else:data['rows'].pop()
            with self.assertRaises(ValueError):validate(data)

    def test_explicit_single_seed_subset_contract(self):
        data=complete();data['rows']=[r for r in data['rows'] if r['seed']==CONTROL_SEEDS[0]]
        data['contracts']=dict(pixel_parities=80,zero_identities=40)
        self.assertEqual(len(validate(data,CONTROL_SEEDS[:1])),20)

    def test_missing_pixel_contract_rejected(self):
        data=complete();data['contracts']['pixel_parities']=159
        with self.assertRaises(ValueError):validate(data)

    def test_nonfinite_metric_and_excess_threads_rejected(self):
        for bad in ('metric','workers'):
            data=complete()
            if bad=='metric':data['rows'][0]['measurements']['noisy_100']['opponent_mse'][0]=float('nan')
            else:data['rows'][0]['measurements']['workers']=9
            with self.assertRaises(ValueError):validate(data)

    def test_missing_external_variant_and_invalid_profile_rejected(self):
        data=complete()
        with self.assertRaises(ValueError):validate(data,external=True)
        data['rows'][0]['measurements']['external_covariance_noisy']=dict(data['rows'][0]['measurements']['noisy_100'])
        data['rows'][0]['external_covariance']=[[[ -1.,0.,0.],[0.,1.,0.],[0.,0.,1.]]]*5
        with self.assertRaises(ValueError):validate(data)


if __name__=='__main__':unittest.main()
