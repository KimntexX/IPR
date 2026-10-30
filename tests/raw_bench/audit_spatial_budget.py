"""Fixed-gate audit, per-draw uncertainty and bias register; no pixel recomputation."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from audit_dark_mean import sha256
from study_spatial_budget import PATTERNS,ROIS


def validate(data,freeze):
    expected={(iso,distribution,pattern,shot) for iso in (1600,51200) for distribution in ('gaussian','scaled_poisson')
              for pattern in PATTERNS for shot in range(2)}
    actual=[(r['iso'],r['distribution'],r['pattern'],r['shot']) for r in data['rows']]
    if len(actual)!=32 or set(actual)!=expected:raise ValueError('Incomplete/duplicate spatial design')
    if data['verdict']!='measured_spatial_budget_and_uncertainty_no_independent_physical_validation':raise ValueError('Failed campaign')
    if data['batch_pixel_parities']!=16:raise ValueError('Missing scalar/batch pixel parities')
    allowed={r['sha256'] for group in freeze['new_transfer_controls'].values() for r in group}
    old={c['source']['sha256'] for pair in freeze['selection'] for c in pair['captures']}
    captured={c['source']['sha256'] for c in data['captures']}
    if len(allowed)!=4 or captured!=allowed or allowed&old:raise ValueError('Selection leakage/duplicate sources')
    for row in data['rows']:
        if len(row['control_identities'])!=32 or set(row['regions'])!=set(ROIS):raise ValueError('Missing draws/regions')
        for entry in row['regions'].values():
            arrays=[entry['control_covariances']]+[m['calibration_covariances'] for m in entry['models'].values()]
            for values in arrays:
                array=np.asarray(values)
                if array.shape!=(32,5,3,3) or not np.isfinite(array).all():raise ValueError('Invalid per-draw measurements')


def metrics(rows,model,patterns):
    return [c for row in rows if row['pattern'] in patterns for entry in row['regions'].values()
            for c in entry['models'][model]['comparison']]


def summary(values):
    errors=np.array([m['relative_frobenius_error'] for m in values]);ratios=np.array([m['sigma_predicted_over_observed'] for m in values])
    se=np.array([m['relative_mc_standard_error'] for m in values])
    if not np.isfinite(errors).all() or not np.isfinite(ratios).all() or not np.isfinite(se).all():raise ValueError('Nonfinite score')
    return dict(comparisons=len(values),covariance_error_percentiles_50_95_100=np.percentile(errors,[50,95,100]).tolist(),
        sigma_ratio_min=float(ratios.min()),sigma_ratio_max=float(ratios.max()),relative_MC_SE_median=float(np.median(se)))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('proof',type=Path);args=p.parse_args()
    root=Path(__file__).resolve().parents[2];data=json.loads(args.proof.read_text(encoding='utf-8'));path=root/data['freeze_path']
    if sha256(path)!=data['freeze_sha256']:raise ValueError('Changed freeze')
    freeze=json.loads(path.read_text(encoding='utf-8'));validate(data,freeze)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,created_utc=stamp,parent_path=str(args.proof),parent_sha256=sha256(args.proof),
        tool_sha256=sha256(Path(__file__)),groups=[],fixed_gate={},bias_register=freeze['limits'])
    for iso in (1600,51200):
        for distribution in ('gaussian','scaled_poisson'):
            rows=[r for r in data['rows'] if r['iso']==iso and r['distribution']==distribution]
            for pattern in PATTERNS:
                models={name:summary(metrics(rows,name,(pattern,))) for name in ('flat','spatial','clean_diagnostic')}
                result['groups'].append(dict(iso=iso,distribution=distribution,pattern=pattern,models=models))
    flat=summary(metrics(data['rows'],'spatial',('flat',)));all_cases=summary(metrics(data['rows'],'spatial',PATTERNS))
    transitions=[p for p in PATTERNS if p!='flat']
    before=summary(metrics(data['rows'],'flat',transitions));after=summary(metrics(data['rows'],'spatial',transitions))
    result['fixed_gate']=dict(flat_median_covariance_le_015=flat['covariance_error_percentiles_50_95_100'][0]<=.15,
        every_sigma_ratio_in_08_125=all_cases['sigma_ratio_min']>=.8 and all_cases['sigma_ratio_max']<=1.25,
        transition_median_improves_flat=after['covariance_error_percentiles_50_95_100'][0]<before['covariance_error_percentiles_50_95_100'][0])
    result['fixed_gate_passed']=all(result['fixed_gate'].values())
    result['conditional_components']=[]
    for model in ('flat','spatial','clean_diagnostic'):
        for pattern in PATTERNS:
            variable_errors=[];common_errors=[]
            for row in data['rows']:
                if row['pattern']!=pattern:continue
                for entry in row['regions'].values():
                    target=entry['control_decomposition'];pred=entry['models'][model]['decomposition']
                    for field,values in (('variable_photon_conditional_covariance',variable_errors),('common_conditional_covariance',common_errors)):
                        for a,b in zip(pred[field],target[field]):values.append(float(np.linalg.norm(np.array(a)-b)/max(np.linalg.norm(b),1e-30)))
            result['conditional_components'].append(dict(model=model,pattern=pattern,variable_median_error=float(np.median(variable_errors)),
                common_median_error=float(np.median(common_errors)),note='Conditional on frozen read frame, not pure photon/read or physical FPN separation.'))
    same=[];other=[]
    for row in data['rows']:
        if row['pattern']=='flat':
            for entry in row['regions'].values():
                same+=entry['same_read_diagnostic']['comparison'];other+=entry['models']['clean_diagnostic']['comparison']
    result['privileged_same_read_vs_other']=dict(same_read=summary(same),other_read=summary(other))
    result['decision']='Research gate passed, qualify independent physical corpus before production.' if result['fixed_gate_passed'] else 'Gate failed: no promotion or threshold retuning; inspect conditional read-field and guide effects.'
    output=root/'docs/validation/raw'/f'spatial-budget-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['fixed_gate'],result['decision'])


if __name__=='__main__':main()
