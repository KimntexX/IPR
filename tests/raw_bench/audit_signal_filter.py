"""Audit immutable same-filter campaigns and expose distribution-sensitive decisions.

No new pixel calculations. One early Poisson freeze retains two Gaussian prose
labels; this audit records that correction explicitly without rewriting evidence.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from audit_dark_mean import sha256
from study_signal_filter import PATTERNS,CONTROL_SEEDS,VARIANTS,summary
from study_cfa_signal_transport import LEVELS


def validate(data,controls=CONTROL_SEEDS,external=False):
    expected={(iso,level,pattern,shot,seed) for iso,levels in LEVELS.items()
              for level in levels for pattern in PATTERNS for shot in range(2) for seed in controls}
    actual=[(r['iso'],r['level'],r['pattern'],r['shot'],r['seed']) for r in data['rows']]
    count=len(expected)
    if len(actual)!=count or len(set(actual))!=count or set(actual)!=expected:
        raise ValueError('Incomplete or duplicated fixed design')
    if data['verdict']!='measured_same_filter_signal_profiles_not_production_validation':
        raise ValueError('Campaign not qualified')
    if data['contracts']!={'pixel_parities':2*count,'zero_identities':count}:
        raise ValueError('Missing pixel contracts')
    for row in data['rows']:
        if not np.isfinite(row['input_rgb_mse']) or row['input_rgb_mse']<=0:raise ValueError('Invalid input MSE')
        if not 1<=row['measurements']['workers']<=8:raise ValueError('Thread budget')
        if external and 'external_covariance_noisy' not in row['measurements']:raise ValueError('Missing external variant')
        if 'external_covariance_noisy' in row['measurements']:
            profile=np.asarray(row.get('external_covariance'),dtype=float)
            if profile.shape!=(5,3,3) or not np.isfinite(profile).all():raise ValueError('Invalid external profile')
            for matrix in profile:
                scale=max(float(np.linalg.norm(matrix)),1e-30)
                if np.linalg.norm(matrix-matrix.T)>scale*1e-9 or np.linalg.eigvalsh(matrix).min()<-scale*1e-9:
                    raise ValueError('Asymmetric or non-PSD external profile')
        for metric in row['measurements'].values():
            if not isinstance(metric,dict) or 'rgb_mse' not in metric:continue
            for field in ('rgb_mse','opponent_mse','opponent_bias'):
                values=np.asarray(metric[field])
                if not np.isfinite(values).all():raise ValueError('Nonfinite metric')
                if field!='opponent_bias' and np.min(values)<0:raise ValueError('Negative MSE')
    return summary(data['rows'])


def aggregate(rows):
    output=[]
    variants_to_measure=dict(VARIANTS)
    if rows and all('external_covariance_noisy' in r['measurements'] for r in rows):
        variants_to_measure['external_covariance']='external_covariance_noisy'
    for iso in LEVELS:
        for pattern in PATTERNS:
            group=[r for r in rows if r['iso']==iso and r['pattern']==pattern]
            variants={}
            for name,key in variants_to_measure.items():
                ratios=[r['measurements'][key]['rgb_mse']/r['measurements']['noisy_100']['rgb_mse'] for r in group]
                opposed=[np.array(r['measurements'][key]['opponent_mse'])/r['measurements']['noisy_100']['opponent_mse'] for r in group]
                variants[name]=dict(median_mse_ratio_to_actual=float(np.median(ratios)),
                    min_mse_ratio_to_actual=min(ratios),max_mse_ratio_to_actual=max(ratios),
                    strictly_better_cases=sum(v<1 for v in ratios),cases=len(ratios),
                    median_opponent_mse_ratio=np.median(opposed,axis=0).tolist())
            output.append(dict(iso=iso,pattern=pattern,variants=variants))
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('proofs',nargs='+',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2]
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,campaign_id=f'signal-filter-audit-v001-{stamp}',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),tool_sha256=sha256(Path(__file__)),
        note='Audit and interpretation of existing pixel campaigns, not new independent validation.',
        campaigns=[])
    repeated_inputs={};same_input_checks={}
    for path in args.proofs:
        data=json.loads(path.read_text(encoding='utf-8'))
        frozen=root/data['freeze_path']
        if sha256(frozen)!=data['freeze_sha256']:raise ValueError('Changed freeze')
        freeze=json.loads(frozen.read_text(encoding='utf-8'))
        checked=validate(data,freeze['protocol']['control_seeds'],freeze['protocol'].get('external_covariance',False))
        distribution=freeze['protocol'].get('distribution','gaussian')
        for row in data['rows']:
            key=(distribution,row['iso'],row['level'],row['pattern'],row['shot'],row['seed'])
            identity=(row['input_sha256'],row['measurements']['noisy_100']['pixels_sha256'])
            if key in repeated_inputs:
                if repeated_inputs[key]!=identity:raise ValueError('Repeated pixels or baseline changed')
                same_input_checks[distribution]=same_input_checks.get(distribution,0)+1
            else:repeated_inputs[key]=identity
        corrections=[]
        if distribution=='scaled_poisson':
            if 'independent Gaussian seed' in freeze['protocol']['reference']:
                corrections.append(dict(field='protocol.reference',original=freeze['protocol']['reference'],
                    corrected=freeze['protocol']['reference'].replace('Gaussian','scaled-Poisson')))
            for index,line in enumerate(freeze['limits']):
                if 'Moment-matched Gaussian signal term' in line:
                    corrections.append(dict(field=f'limits[{index}]',original=line,
                        corrected=line.replace('Gaussian','scaled-Poisson')))
        result['campaigns'].append(dict(path=str(path),sha256=sha256(path),freeze_sha256=sha256(frozen),
            distribution=distribution,metadata_clarifications=corrections,
            rationale='Structured distribution field and frozen normalized_draws calls identify the executed distribution; prose corrections do not change measurements.',
            seconds=data['seconds'],contracts=data['contracts'],per_level_summary=checked,
            aggregate=aggregate(data['rows'])))
    result['repeated_input_and_baseline_parities_by_distribution']=same_input_checks
    result['decision']='Audit only; no automatic promotion, threshold tuning or physical sensor qualification.'
    output=root/'docs/validation/raw'/f'signal-filter-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output)


if __name__=='__main__':main()
