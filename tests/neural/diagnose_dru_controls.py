"""Read-only paired diagnosis of all six runs and all saved control milestones."""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import statistics

from campaign_artifacts import sha256


def aggregate(records, cases):
    """Resolve physical CFA colour through site tags, never through site position."""
    buckets=defaultdict(list)
    for row in records:
        case=cases[row['case_index']]
        if len(row['site_colours'])!=4 or sorted(row['site_colours'])!=list(range(4)):
            raise ValueError('Four distinct CFA colours required')
        if any(row[k]!=case[k] for k in ('frequency','chromatic','angle')):
            raise ValueError('Control case identity changed')
        for colour in range(4):
            site=row['site_colours'].index(colour)
            corrected=row['corrected'][site];observed=row['observed'][site]
            values=(corrected['sine_gain'],corrected['quadrature_gain'],corrected['dc_error'],corrected['mse'],observed['mse'])
            if not all(math.isfinite(v) for v in values) or min(values[-2:])<0:
                raise ValueError('Finite gains and nonnegative MSE required')
            key=(case['frequency'],case['amplitude'],case['chromatic'],row['noise_index'],colour)
            buckets[key].append(values)
    output=[]
    for key,values in sorted(buckets.items()):
        baseline=sum(v[4] for v in values)
        if baseline<=0:raise ValueError('Positive observed MSE required')
        output.append(dict(frequency=key[0],amplitude=key[1],chromatic=key[2],noise_index=key[3],colour=key[4],
            fields=len(values),mean_gain=statistics.mean(v[0] for v in values),
            min_field_gain=min(v[0] for v in values),
            mean_abs_quadrature=statistics.mean(abs(v[1]) for v in values),
            mean_abs_dc_error=statistics.mean(abs(v[2]) for v in values),
            mse_ratio=sum(v[3] for v in values)/baseline))
    return output


def key(row):
    return tuple(row[k] for k in ('frequency','amplitude','chromatic','noise_index','colour'))


def run(campaign, output):
    root=Path(__file__).resolve().parents[2]
    source=root/'build-patch_neural_restoration/curve-known-fine-v001/result.json'
    cases=json.loads(source.read_text(encoding='utf-8'))['plan']['cases']
    result=dict(schema_version=1,status='started',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        sources=[dict(path=str(source),sha256=sha256(source))],runs=[],groups=[],trajectories=[],paired_effects=[],
        limits=['Known development controls only; post-hoc diagnosis, no independent validation.',
            'All available milestones retained: 0,128,256. No threshold or weight selection.',
            'Strong controls have a single amplitude: cannot infer amplitude dependence from these measurements.',
            'Frequency is the generator parameter on the diagnostic field, not cycles per pixel.',
            'Training losses sample different observations over time: window trends do not prove convergence.',
            'Data-policy interventions isolate policies, not the individual components within each policy.',
            'No causal attribution to architecture, objective or asymptotic convergence without a matched experiment.'])
    controls={};states={};observations=None
    for arm in ('reference','noise','details'):
        for seed in (71,73):
            folder=campaign/arm/f'seed{seed}';path=folder/'result.json'
            state=json.loads(path.read_text(encoding='utf-8'));states[arm,seed]=state
            if state['status']!='completed_dru_bootstrap_not_photographic_qualification' or state['completed_steps']!=256:
                raise ValueError('Six complete 256-step runs required')
            if state['data_arm']!=arm or state['data_seed']!=71 or state['seed']!=seed:
                raise ValueError('Unpaired run identities')
            result['sources'].append(dict(path=str(path),sha256=sha256(path)))
            losses=state['losses']
            if len(losses)!=256 or not all(math.isfinite(v) for v in losses):raise ValueError('Complete finite losses required')
            result['runs'].append(dict(arm=arm,seed=seed,training_mean_first32=statistics.mean(losses[:32]),
                training_mean_last32=statistics.mean(losses[-32:]),losses=losses))
            for step in (0,128,256):
                path=folder/f'control-{step:06d}.json';control=json.loads(path.read_text(encoding='utf-8'))
                result['sources'].append(dict(path=str(path),sha256=sha256(path)))
                identities=[tuple(r[k] for k in ('case_index','phase_index','noise_index','replicate','observation_sha256','sigma_sha256')) for r in control['records']]
                if len(identities)!=864 or len(set(identities))!=864:raise ValueError('Full distinct controls required')
                if observations is not None and identities!=observations:raise ValueError('Control observations changed')
                observations=identities
                rows=aggregate(control['records'],cases);controls[arm,seed,step]={key(r):r for r in rows}
                result['groups'].extend(dict(arm=arm,seed=seed,step=step,**r) for r in rows)
    for seed in (71,73):
        if len({states[a,seed]['initial_model_fingerprint'] for a in ('reference','noise','details')})!=1:
            raise ValueError('Initial weights are not paired')
        for arm in ('reference','noise','details'):
            if controls[arm,seed,0]!=controls['reference',seed,0]:raise ValueError('Initial controls differ')
            for k,final in controls[arm,seed,256].items():
                middle=controls[arm,seed,128][k]
                result['trajectories'].append(dict(arm=arm,seed=seed,**{n:final[n] for n in ('frequency','amplitude','chromatic','noise_index','colour')},
                    gain_at128=middle['mean_gain'],gain_at256=final['mean_gain'],
                    late_gain_delta=final['mean_gain']-middle['mean_gain'],
                    late_mse_delta=final['mse_ratio']-middle['mse_ratio']))
                if arm!='reference':
                    ref=controls['reference',seed,256][k]
                    result['paired_effects'].append(dict(arm=arm,seed=seed,**{n:final[n] for n in ('frequency','amplitude','chromatic','noise_index','colour')},
                        gain_delta=final['mean_gain']-ref['mean_gain'],mse_ratio_delta=final['mse_ratio']-ref['mse_ratio']))
    result['status']='completed_read_only_dru_control_diagnosis'
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    canonical=root/'docs/validation/raw'/f'neural-dru-control-diagnosis-v001-{stamp}.json'
    with canonical.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    print(canonical,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(args.campaign,args.output)
