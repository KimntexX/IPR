"""Verify paired routing ablation without treating reused inputs as fresh data."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from audit_dark_mean import sha256
from study_local_budget import gates,ROUTES


def validate(data,parent):
    key=lambda r:tuple(r[k] for k in ('iso','domain','distribution','pattern','seed'))
    previous={key(r):r for r in parent['rows']}
    if len(data['rows'])!=64 or len(previous)!=64 or {key(r) for r in data['rows']}!=set(previous):raise ValueError('Wrong paired grid')
    if data['verdict']!='measured_local_routing_ablation_not_production_validation' or data['parities']!=640:raise ValueError('Incomplete campaign')
    for row in data['rows']:
        old=previous[key(row)]
        if row['input_sha256']!=old['input_sha256'] or row['transport']!=old['transport']:raise ValueError('Changed input/transport')
        if row['input_rgb_mse']!=old['input_rgb_mse']:raise ValueError('Changed input MSE')
        for candidate in ('spectral','bank'):
            measured=row['measurements'][candidate]
            for name in ('noisy_100','clean_100','external_covariance_noisy','external_covariance_clean_stress','zero_model_clean'):
                if measured[name]!=old['measurements'][candidate][name]:raise ValueError('Changed original metrics/hash')
            for route in ROUTES:
                for suffix in ('noisy','clean_stress'):
                    m=measured[f'external_{route}_{suffix}']
                    values=[m['rgb_mse'],*m['opponent_mse'],*m['opponent_bias']]
                    if m['pattern_gain'] is not None:values.append(m['pattern_gain'])
                    if not np.isfinite(values).all():raise ValueError('Nonfinite measurement')
    if data['gates']!=gates(data['rows']):raise ValueError('Changed gate')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('proof',type=Path);args=p.parse_args()
    root=Path(__file__).resolve().parents[2];data=json.loads(args.proof.read_text(encoding='utf-8'))
    path=root/data['freeze_path']
    if sha256(path)!=data['freeze_sha256']:raise ValueError('Changed freeze')
    freeze=json.loads(path.read_text(encoding='utf-8'));parent_path=Path(freeze['parent']['path'])
    if sha256(parent_path)!=freeze['parent']['sha256']:raise ValueError('Changed parent')
    parent=json.loads(parent_path.read_text(encoding='utf-8'));validate(data,parent)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,created_utc=stamp,parent_path=str(args.proof),parent_sha256=sha256(args.proof),
        tool_sha256=sha256(Path(__file__)),gates=data['gates'],threads=freeze['threads'],limits=freeze['limits'],seconds=data['seconds'],groups=[])
    for domain in ('spectral_internal','real_read_replay'):
        for route in ROUTES:
            for candidate in ('spectral','bank'):
                for pattern in ('flat','weak_luma','weak_color','curved_edge'):
                    rows=[r for r in data['rows'] if r['domain']==domain and r['pattern']==pattern]
                    ratios=[r['measurements'][candidate][f'external_{route}_noisy']['rgb_mse']/r['measurements'][candidate]['noisy_100']['rgb_mse'] for r in rows]
                    result['groups'].append(dict(domain=domain,route=route,candidate=candidate,pattern=pattern,cases=len(rows),
                        mse_ratio_p50_p95_max=np.percentile(ratios,[50,95,100]).tolist(),
                        changed_noisy_output_hashes=sum(r['measurements'][candidate][f'external_{route}_noisy']['pixels_sha256']!=r['measurements'][candidate]['noisy_100']['pixels_sha256'] for r in rows)))
    result['decision']='Paired ablation only. Retain all failed gates; no production promotion or independent data claim.'
    output=root/'docs/validation/raw'/f'local-budget-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,json.dumps(result['gates']),flush=True)


if __name__=='__main__':main()
