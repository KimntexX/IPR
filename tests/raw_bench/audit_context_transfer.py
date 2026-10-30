"""Audit contextual budget transfer without recomputing any pixels."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from audit_dark_mean import sha256
from study_context_transfer import PATTERNS,ROIS


def validate(data,freeze):
    if data['verdict']!='measured_contextual_budget_transfer_not_photo_or_production_validation':raise ValueError('Campaign failed')
    expected={(iso,context,distribution,pattern,shot) for iso in (1600,51200)
              for context in freeze['protocol']['context_variants'] for distribution in freeze['protocol']['distributions']
              for pattern in PATTERNS for shot in range(2)}
    keys=[(r['iso'],r['context'],r['distribution'],r['pattern'],r['shot']) for r in data['rows']]
    if len(keys)!=len(expected) or set(keys)!=expected:raise ValueError('Incomplete or duplicate design')
    for row in data['rows']:
        if set(row['regions'])!=set(ROIS) or len(row['control_identities'])!=4:raise ValueError('Incomplete regions/controls')
        if any(not 1<=v['workers']<=8 for v in row['control_identities']):raise ValueError('Thread budget exceeded')
        for entry in row['regions'].values():
            if len(entry['comparisons'])!=5:raise ValueError('Incomplete B3 levels')
            if len(entry['profile']['key'])!=64:raise ValueError('Missing profile identity')
            for level in entry['comparisons']:
                for field in ('predicted_covariance','observed_covariance','sigma_predicted_over_observed','relative_frobenius_error'):
                    if not np.isfinite(np.asarray(level[field],dtype=float)).all():raise ValueError('Nonfinite transfer metric')
                if not level['positive_semidefinite']:raise ValueError('Invalid covariance')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('proof',type=Path)
    parser.add_argument('--baseline',type=Path,help='Verify identical inputs and control covariances for geometry replay.')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];data=json.loads(args.proof.read_text(encoding='utf-8'))
    frozen=root/data['freeze_path']
    if sha256(frozen)!=data['freeze_sha256']:raise ValueError('Changed freeze')
    freeze=json.loads(frozen.read_text(encoding='utf-8'));validate(data,freeze)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,created_utc=stamp,parent_path=str(args.proof),parent_sha256=sha256(args.proof),
                tool_sha256=sha256(Path(__file__)),normalization_witnesses=[],groups=[])
    if args.baseline:
        baseline=json.loads(args.baseline.read_text(encoding='utf-8'))
        old_frozen=root/baseline['freeze_path']
        if sha256(old_frozen)!=baseline['freeze_sha256']:raise ValueError('Changed baseline freeze')
        validate(baseline,json.loads(old_frozen.read_text(encoding='utf-8')))
        key=lambda r:(r['iso'],r['context'],r['distribution'],r['pattern'],r['shot'])
        previous={key(r):r for r in baseline['rows']};input_checks=0;covariance_checks=0
        for row in data['rows']:
            old=previous[key(row)]
            if old['control_identities']!=row['control_identities']:raise ValueError('Replay pixels changed')
            input_checks+=4
            for region,entry in row['regions'].items():
                if entry['profile']['roi_yxhw']!=list(ROIS[region]):raise ValueError('Profile geometry mismatch')
                if entry['profile']['mean_rgb']!=old['regions'][region]['profile']['mean_rgb']:raise ValueError('Guide changed')
                for a,b in zip(entry['comparisons'],old['regions'][region]['comparisons']):
                    if a['observed_covariance']!=b['observed_covariance']:raise ValueError('Control covariance changed')
                    covariance_checks+=1
        result['geometry_replay']=dict(baseline_path=str(args.baseline),baseline_sha256=sha256(args.baseline),
                                      input_identity_parities=input_checks,observed_covariance_parities=covariance_checks)
    for bundle in data['contexts']:
        for name,context in bundle['variants'].items():
            for phase,p in context['phases'].items():
                for signal in (0.,sum(p['fit_range_dn'])/2,p['fit_range_dn'][1]):
                    mean=p['gain']*(signal+p['reference_black']-p['runtime_black'])/context['divisor']
                    predicted=p['A']*mean+p['B'];expected=(p['b']+p['a']*signal)*(p['gain']/context['divisor'])**2
                    if not np.isclose(predicted,expected,rtol=1e-12,atol=1e-20):raise ValueError('Affine variance normalization failed')
                    result['normalization_witnesses'].append(dict(iso=context['iso'],context=name,phase=phase,
                        latent_signal_dn=signal,balanced_mean=mean,affine_variance=predicted,direct_variance=expected))
    for key in sorted({(r['iso'],r['context'],r['distribution'],r['pattern']) for r in data['rows']}):
        rows=[r for r in data['rows'] if (r['iso'],r['context'],r['distribution'],r['pattern'])==key]
        regions={}
        for region in ROIS:
            bands=[]
            for level in range(5):
                metrics=[r['regions'][region]['comparisons'][level] for r in rows]
                ratios=np.array([m['sigma_predicted_over_observed'] for m in metrics])
                bands.append(dict(level=level,median_covariance_relative_error=float(np.median([m['relative_frobenius_error'] for m in metrics])),
                    min_sigma_ratio_per_axis=ratios.min(axis=0).tolist(),max_sigma_ratio_per_axis=ratios.max(axis=0).tolist()))
            regions[region]=bands
        result['groups'].append(dict(iso=key[0],context=key[1],distribution=key[2],pattern=key[3],regions=regions))
    result['content_summary']=[]
    for pattern in PATTERNS:
        rows=[r for r in data['rows'] if r['pattern']==pattern]
        for bands in ((0,1,2),(3,4)):
            metrics=[r['regions'][region]['comparisons'][level] for r in rows for region in ROIS for level in bands]
            errors=[m['relative_frobenius_error'] for m in metrics]
            ratios=np.array([m['sigma_predicted_over_observed'] for m in metrics])
            result['content_summary'].append(dict(pattern=pattern,levels=list(bands),comparisons=len(metrics),
                covariance_error_percentiles_50_95_100=np.percentile(errors,[50,95,100]).tolist(),
                sigma_ratio_min=float(ratios.min()),sigma_ratio_max=float(ratios.max())))
    result['uncertainty_scope']='Descriptive spatial moments on finite correlated 64px windows, three control draws and four calibration draws; not confidence intervals or independent real acquisitions.'
    result['verdict']='audited_contextual_simulation_only_no_automatic_transfer_or_production_validation'
    output=root/'docs/validation/raw'/f'context-transfer-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output)


if __name__=='__main__':main()
