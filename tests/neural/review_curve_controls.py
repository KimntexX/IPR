"""Read-only matched review of known controls; never relax a failed criterion."""
from datetime import datetime,timezone
import json
from pathlib import Path
from campaign_artifacts import sha256


def fine_summary(result):
    criteria=result['plan']['criteria']
    strong=[r for r in result['groups'] if r['amplitude']==criteria['strong_amplitude']]
    return dict(passed=result['pilot_gates_passed'],overall=result['overall'],
        all_groups=result['groups'],weak_groups_are_descriptive_not_gating=True,
        strong_failures=[dict(seed=r['seed'],noise_index=r['noise_index'],frequency=r['frequency'],chromatic=r['chromatic'],
            gain_over_parent=r['gain_over_parent'],rgb_over_parent=r['rgb_over_parent'],sensor_over_observed=r['sensor_over_observed'],
            checks=r['strong_checks']) for r in strong if not r['strong_passed']],strong_groups=len(strong))


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    progress_path=build/'curve-known-controls-v001/progress.json';progress=json.loads(progress_path.read_text(encoding='utf-8'))
    if not progress['status'].startswith('completed_'):raise ValueError('Known-control chain not complete')
    path=build/'curve-known-development-v001/result.json';development=json.loads(path.read_text(encoding='utf-8'))
    result=dict(schema_version=1,status='completed_read_only_known_control_review',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        progress_sha256=sha256(progress_path),chain_status=progress['status'],development_sha256=sha256(path),
        development_passed=development['pilot_gates_passed'],seeds=[],regressions=progress['regressions'],
        limits=['Known development/regression only; no independent photographic qualification.',
                'Original synthetic noise and learned reconstruction baseline, not sensor calibration or RCD truth.',
                'No threshold or model selection changed; all failed cases retained.'])
    old_development=Path(development['plan']['initial_development_proof'])
    if sha256(old_development)!=development['plan']['initial_development_sha256']:
        raise ValueError('Initial development proof changed')
    for row in development['models']:
        result['seeds'].append(dict(seed=row['seed'],passed=row['pilot_gates_passed'],
            zero_checks={k:row[k]['qualification']['passed'] for k in ('zero_new','zero_new20','zero_historical80','zero_historical20')},
            distinct_edge_zero=row['zero_distinct_edge_checks'],noise_mixed=row['noise_mixed'],noise_all=row['noise_all']))
    fine_path=build/'curve-known-fine-v001/result.json'
    if fine_path.exists():
        current=json.loads(fine_path.read_text(encoding='utf-8'))
        original_path=build/'repeatability-control-fine-v001/result.json';original=json.loads(original_path.read_text(encoding='utf-8'))
        for name in ('cases','fields','criteria'):
            if current['plan'][name]!=original['plan'][name]:raise ValueError('Historical fine data/criteria changed')
        identity=lambda r:(r['seed'],r['case_index'],r['phase_index'],r['noise_index'],r['replicate'],r['target_sha256'],r['observation_sha256'],r['sigma_sha256'],r['parent'])
        if [identity(r) for r in current['records']]!=[identity(r) for r in original['records']]:
            raise ValueError('Fine observations or parent metrics differ from known baseline')
        result['fine']=dict(current_sha256=sha256(fine_path),initial_sha256=sha256(original_path),
            current=fine_summary(current),initial=fine_summary(original),all4032_inputs_and_parent_replayed=True)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-curve-controls-review-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print('CHAIN',result['chain_status'],'DEVELOPMENT',result['development_passed'],flush=True)
    if 'fine' in result:print('FINE FAILURES',len(result['fine']['initial']['strong_failures']),'->',len(result['fine']['current']['strong_failures']),flush=True)


if __name__=='__main__':run()
