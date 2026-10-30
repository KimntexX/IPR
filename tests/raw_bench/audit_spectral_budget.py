"""Recompute gates, retain separate domains, and audit population-benchmark scope."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
from audit_dark_mean import sha256
from study_spectral_budget import gates,DOMAINS
from study_spatial_budget import compare_draws,PATTERNS,ROIS
from audit_spatial_budget import summary


def validate(data,freeze):
    expected={(iso,domain,distribution,pattern) for iso in (1600,51200) for domain in DOMAINS
              for distribution in ('gaussian','scaled_poisson') for pattern in PATTERNS}
    keys=[(r['iso'],r['domain'],r['distribution'],r['pattern']) for r in data['rows']]
    if len(keys)!=32 or set(keys)!=expected:raise ValueError('Missing/duplicate population cases')
    if data['verdict']!='measured_population_budget_domains_separate_not_physical_validation':raise ValueError('Failed study')
    calibration={c['source']['sha256'] for p in freeze['selection'] for c in p['captures']}|{c['sha256'] for c in freeze['extras'].values()}
    controls={c['source']['sha256'] for c in freeze['real_controls']}
    if len(calibration)!=6 or len(controls)!=4 or calibration&controls:raise ValueError('Calibration/control source overlap')
    if any(m['physical_calibration_frames']!=3 or m['summary']['calibration_tiles']!=9 for m in data['models']):raise ValueError('Incorrect physical frame count')
    for row in data['rows']:
        if len(row['control_identities'])!=32 or set(row['regions'])!=set(ROIS):raise ValueError('Incomplete draw/region grid')
        if any(not 1<=i['workers']<=8 for i in row['control_identities']):raise ValueError('Worker budget')
        for entry in row['regions'].values():
            if set(entry['models'])!={'flat','spatial','clean_diagnostic'}:raise ValueError('Missing candidate')
            for model in entry['models'].values():
                fresh=compare_draws(model['calibration_covariances'],entry['control_covariances'])
                if fresh!=model['comparison']:raise ValueError('Stored covariance metrics changed')
    if gates(data['rows'])!=data['gates']:raise ValueError('Saved gate differs from frozen computation')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('proof',type=Path);args=p.parse_args()
    root=Path(__file__).resolve().parents[2];data=json.loads(args.proof.read_text(encoding='utf-8'));path=root/data['freeze_path']
    if sha256(path)!=data['freeze_sha256']:raise ValueError('Changed freeze')
    freeze=json.loads(path.read_text(encoding='utf-8'));validate(data,freeze)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,created_utc=stamp,parent_path=str(args.proof),parent_sha256=sha256(args.proof),
        tool_sha256=sha256(Path(__file__)),gates=data['gates'],groups=[],bias_register=freeze['limits'],
        note='No comparison to old single-read target is claimed causal: read policy, seeds and conditioning changed. New criteria remain unchanged.')
    for domain in DOMAINS:
        for model in ('flat','spatial','clean_diagnostic'):
            for levels in ((0,1,2),(3,4)):
                values=[r['regions'][region]['models'][model]['comparison'][level] for r in data['rows'] if r['domain']==domain for region in ROIS for level in levels]
                result['groups'].append(dict(domain=domain,model=model,levels=list(levels),summary=summary(values)))
    result['decision']='Population covariance research candidate passes both frozen gates; real-photo validation, uncertainty and performance remain unqualified.' if all(v['passed'] for v in data['gates'].values()) else 'No promotion: at least one separate-domain frozen gate failed.'
    output=root/'docs/validation/raw'/f'spectral-budget-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['decision'])


if __name__=='__main__':main()
