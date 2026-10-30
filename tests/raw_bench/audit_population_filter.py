"""Recompute fixed same-filter gates and expose pattern-level regressions."""
import argparse
from datetime import datetime, timezone
import itertools
import json
from pathlib import Path
import numpy as np
from audit_dark_mean import sha256
from study_population_filter import gates, PATTERNS, DOMAINS, CONTROLS


def validate(data):
    expected=set(itertools.product((1600,51200),DOMAINS,('gaussian','scaled_poisson'),PATTERNS,CONTROLS))
    keys=[tuple(row[k] for k in ('iso','domain','distribution','pattern','seed')) for row in data['rows']]
    if len(keys)!=64 or set(keys)!=expected:raise ValueError('Incomplete/duplicate case grid')
    if data['verdict']!='measured_same_filter_population_budget_not_production_validation':raise ValueError('Failed campaign')
    if data['contracts']!=dict(parities=256,zero_identities=128):raise ValueError('Incomplete parity contracts')
    for row in data['rows']:
        if not 1<=row['transport']['workers']<=8:raise ValueError('Worker budget')
        if set(row['measurements'])!={'spectral','bank'}:raise ValueError('Missing comparator')
        first=row['measurements']['spectral']
        for name,measurements in row['measurements'].items():
            samples=np.asarray(row['calibration_covariances'][name])
            if samples.shape!=(32,5,3,3) or not np.isfinite(samples).all():raise ValueError('Calibration lost')
            profile=np.asarray(row['covariance_profiles'][name])
            if not np.array_equal(samples.mean(axis=0),profile):raise ValueError('Profile not fixed mean')
            for key in ('noisy_100','clean_100'):
                if measurements[key]['pixels_sha256']!=first[key]['pixels_sha256']:raise ValueError('Different baseline')
            if measurements['zero_model_clean']['pixels_sha256']!=row['input_sha256']['truth']:raise ValueError('Zero mismatch')
            for key in ('noisy_100','clean_100','external_covariance_noisy','external_covariance_clean_stress'):
                m=measurements[key]
                values=[m['rgb_mse'],*m['opponent_mse'],*m['opponent_bias']]
                if m['pattern_gain'] is not None:values.append(m['pattern_gain'])
                if not np.isfinite(values).all():raise ValueError('Nonfinite measurement')
    if gates(data['rows'])!=data['gates']:raise ValueError('Changed saved gates')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('proof',type=Path);args=p.parse_args()
    root=Path(__file__).resolve().parents[2];data=json.loads(args.proof.read_text(encoding='utf-8'))
    frozen=root/data['freeze_path']
    if sha256(frozen)!=data['freeze_sha256']:raise ValueError('Changed freeze')
    freeze=json.loads(frozen.read_text(encoding='utf-8'));validate(data)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,data_version=1,created_utc=stamp,parent_path=str(args.proof),parent_sha256=sha256(args.proof),
        tool_sha256=sha256(Path(__file__)),gates=data['gates'],limits=freeze['limits'],groups=[],extremes=[],paired_comparator=[],seconds=data['seconds'])
    for domain in DOMAINS:
        domain_rows=[r for r in data['rows'] if r['domain']==domain]
        paired=[r['measurements']['spectral']['external_covariance_noisy']['rgb_mse']/
                r['measurements']['bank']['external_covariance_noisy']['rgb_mse'] for r in domain_rows]
        result['paired_comparator'].append(dict(domain=domain,cases=len(paired),
            spectral_over_bank_mse_p50_p95_max=np.percentile(paired,[50,95,100]).tolist(),
            spectral_better_cases=sum(value<1 for value in paired),
            note='Descriptive paired controls, not independent-camera significance or evidence that FFT is indispensable.'))
        for candidate in ('spectral','bank'):
            for iso in (1600,51200):
                for pattern in PATTERNS:
                    rows=[r for r in data['rows'] if (r['domain'],r['iso'],r['pattern'])==(domain,iso,pattern)]
                    ratios=[r['measurements'][candidate]['external_covariance_noisy']['rgb_mse']/r['measurements'][candidate]['noisy_100']['rgb_mse'] for r in rows]
                    gains=[r['measurements'][candidate]['external_covariance_clean_stress']['pattern_gain'] for r in rows if pattern!='flat']
                    noisy_gains=[r['measurements'][candidate]['external_covariance_noisy']['pattern_gain'] for r in rows if pattern!='flat']
                    baseline_gains=[r['measurements'][candidate]['noisy_100']['pattern_gain'] for r in rows if pattern!='flat']
                    result['groups'].append(dict(domain=domain,candidate=candidate,iso=iso,pattern=pattern,cases=len(rows),
                        median_mse_ratio=float(np.median(ratios)),worst_mse_ratio=max(ratios),
                        median_clean_stress_gain=float(np.median(gains)) if gains else None,
                        median_noisy_gain=float(np.median(noisy_gains)) if noisy_gains else None,
                        median_baseline_noisy_gain=float(np.median(baseline_gains)) if baseline_gains else None))
            for r in sorted([r for r in data['rows'] if r['domain']==domain],reverse=True,
                key=lambda r:r['measurements'][candidate]['external_covariance_noisy']['rgb_mse']/r['measurements'][candidate]['noisy_100']['rgb_mse'])[:3]:
                result['extremes'].append(dict(domain=domain,candidate=candidate,iso=r['iso'],pattern=r['pattern'],distribution=r['distribution'],seed=r['seed'],
                    mse_ratio=r['measurements'][candidate]['external_covariance_noisy']['rgb_mse']/r['measurements'][candidate]['noisy_100']['rgb_mse']))
    result['decision']='Research only; expose separate gates and every failed criterion. No production promotion or independent-photo claim.'
    result['interpretation_limits']=[
        'The frozen clean-stress veto fails; do not silently relax it. This stress deliberately supplies nonzero noise to a clean image, unlike the baseline re-estimated from clean content.',
        'Large clean-stress gain loss is not a measured real-photo detail loss. Noisy-detail projection is reported separately and remains conditional on two physical darks.',
        'ExternalCovariance replaces conditional nodes with one pooled profile; same filter arithmetic and force, but not same model spatial routing. Cause is not isolated.',
        'Lower MSE may favor a stronger smoother. Bank beats spectral on these MSE controls, not proof of better perceived detail or universally superior noise estimation.']
    output=root/'docs/validation/raw'/f'population-filter-audit-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,json.dumps(result['gates']),flush=True)


if __name__=='__main__':main()
