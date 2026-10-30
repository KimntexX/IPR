import unittest
import numpy as np
from audit_spectral_budget import validate
from study_spectral_budget import gates,DOMAINS
from study_spatial_budget import PATTERNS,ROIS,compare_draws


def design():
    values=np.broadcast_to(np.eye(3),(32,5,3,3)).tolist()
    models={name:dict(calibration_covariances=values,comparison=compare_draws(values,values)) for name in ('flat','spatial','clean_diagnostic')}
    rows=[dict(iso=iso,domain=domain,distribution=distribution,pattern=pattern,
        control_identities=[dict(workers=8)]*32,regions={name:dict(control_covariances=values,models=models) for name in ROIS})
        for iso in (1600,51200) for domain in DOMAINS for distribution in ('gaussian','scaled_poisson') for pattern in PATTERNS]
    freeze=dict(selection=[dict(captures=[dict(source=dict(sha256=f'cal{i}')) for i in range(4)])],
        extras={str(i):dict(sha256=f'extra{i}') for i in range(2)},real_controls=[dict(source=dict(sha256=f'control{i}')) for i in range(4)])
    data=dict(rows=rows,verdict='measured_population_budget_domains_separate_not_physical_validation',
        models=[dict(physical_calibration_frames=3,summary=dict(calibration_tiles=9))]*2,gates=gates(rows))
    return data,freeze


class SpectralAuditContracts(unittest.TestCase):
    def test_complete_design_recomputed(self):
        data,freeze=design();validate(data,freeze)

    def test_control_overlap_and_inflated_physical_count_fail(self):
        data,freeze=design();freeze['real_controls'][0]['source']['sha256']='cal0'
        with self.assertRaises(ValueError):validate(data,freeze)
        data,freeze=design();data['models'][0]['physical_calibration_frames']=9
        with self.assertRaises(ValueError):validate(data,freeze)

    def test_saved_gate_cannot_hide_failure(self):
        data,freeze=design();data['gates']['real_read_replay']['passed']=not data['gates']['real_read_replay']['passed']
        with self.assertRaises(ValueError):validate(data,freeze)


if __name__=='__main__':unittest.main()
