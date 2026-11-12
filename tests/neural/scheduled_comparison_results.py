"""Predeclared paired advancement gates; never choose a seed or optimize thresholds."""
import json
from pathlib import Path
import statistics


def metrics(control):
    groups=control['groups'];gains=[v for g in groups for v in g['mean_sensor_gain_by_colour']]
    return dict(groups_below095=sum(min(g['mean_sensor_gain_by_colour'])<.95 for g in groups),
        minimum_gain=min(gains),maximum_gain=max(gains),median_mse_ratio=statistics.median(g['sensor_mse_ratio'] for g in groups),
        maximum_mse_ratio=max(g['sensor_mse_ratio'] for g in groups))


def strict(m):
    return m['minimum_gain']>=.95 and m['maximum_gain']<=1.05 and m['maximum_mse_ratio']<=1.000001 and m['median_mse_ratio']<=.90


def promising(candidate,reference):
    nonworse=(candidate['groups_below095']<=reference['groups_below095'] and candidate['minimum_gain']>=reference['minimum_gain']
        and candidate['median_mse_ratio']<=reference['median_mse_ratio'] and candidate['minimum_gain']>=0)
    improvement=(candidate['groups_below095']<reference['groups_below095'] or candidate['median_mse_ratio']<reference['median_mse_ratio'])
    return nonworse and improvement


def assess(folders,step):
    rows=[];states={};control_ids=None
    for arm,folder in folders.items():
        for seed in (71,73):
            path=Path(folder)/f'seed{seed}';state=json.loads((path/'result.json').read_text(encoding='utf-8'))
            if state['status']!='completed_dru_bootstrap_not_photographic_qualification' or state['completed_steps']!=step:
                raise ValueError('All declared complete jobs required')
            if state['data_arm']!=arm or state['data_seed']!=71:raise ValueError('Unpaired data identities')
            c=json.loads((path/f'control-{step:06d}.json').read_text(encoding='utf-8'))
            ids=[r['observation_sha256'] for r in c['records']]
            if len(ids)!=864 or control_ids is not None and ids!=control_ids:raise ValueError('Control observations changed')
            control_ids=ids;states[arm,seed]=state
            rows.append(dict(arm=arm,seed=seed,step=step,**metrics(c)))
    for seed in (71,73):
        if len({s['initial_model_fingerprint'] for (a,k),s in states.items() if k==seed})!=1:raise ValueError('Unpaired initializations')
    by={(r['arm'],r['seed']):r for r in rows}
    candidates=[a for a in folders if a!='reference']
    return dict(step=step,rows=rows,
        strict_candidates=[a for a in candidates if all(strict(by[a,k]) for k in (71,73))],
        promising_candidates=[a for a in candidates if all(promising(by[a,k],by['reference',k]) for k in (71,73))],
        scope='Known development numeric gates only; not independent or photographic validation. Thresholds fixed before scheduling.')
