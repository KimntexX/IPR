import copy
import unittest
from audit_context_transfer import validate
from study_context_transfer import PATTERNS,ROIS


def design():
    protocol=dict(context_variants=['metadata','WB_witness','normalization_witness'],distributions=['gaussian','scaled_poisson'])
    metric=dict(predicted_covariance=[[1.,0,0],[0,1.,0],[0,0,1.]],observed_covariance=[[1.,0,0],[0,1.,0],[0,0,1.]],
                sigma_predicted_over_observed=[1.,1.,1.],relative_frobenius_error=0.,positive_semidefinite=True)
    entry=dict(profile=dict(key='a'*64),comparisons=[copy.deepcopy(metric) for _ in range(5)])
    rows=[dict(iso=iso,context=c,distribution=d,pattern=p,shot=s,control_identities=[{'workers':8}]*4,
        regions={r:copy.deepcopy(entry) for r in ROIS}) for iso in (1600,51200) for c in protocol['context_variants']
        for d in protocol['distributions'] for p in PATTERNS for s in range(2)]
    return dict(verdict='measured_contextual_budget_transfer_not_photo_or_production_validation',rows=rows),dict(protocol=protocol)


class TransferAuditContracts(unittest.TestCase):
    def test_complete_grid(self):
        data,freeze=design();validate(data,freeze)

    def test_duplicate_and_missing_region_fail(self):
        data,freeze=design();data['rows'][-1]=data['rows'][0]
        with self.assertRaises(ValueError):validate(data,freeze)
        data,freeze=design();data['rows'][0]['regions'].pop('edge')
        with self.assertRaises(ValueError):validate(data,freeze)

    def test_nonfinite_metric_fails(self):
        data,freeze=design();data['rows'][0]['regions']['left']['comparisons'][0]['relative_frobenius_error']=float('nan')
        with self.assertRaises(ValueError):validate(data,freeze)


if __name__=='__main__':unittest.main()
