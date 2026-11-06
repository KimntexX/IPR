"""Paired effects for every declared arm/seed/milestone, without choosing a winner."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import statistics
from campaign_artifacts import sha256


def run(campaign):
    root=Path(__file__).resolve().parents[2]
    result=dict(schema_version=1,status='started',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        records=[],paired_effects=[],initialization_checks=[],limits=[
            'Known development controls only, not an independent confirmation or photo-quality proof.',
            'Changes measure data-policy effects at256steps; not asymptotic convergence.',
            'Noise changes distribution, and detail policy changes both frequency and amplitude coverage; no attribution within those policies.',
            'No new camera characterisation, no pretrained weights, no photographic rendering.'])
    states={};controls={}
    for arm in ('reference','noise','details'):
        for seed in (71,73):
            path=campaign/arm/f'seed{seed}/result.json';state=json.loads(path.read_text(encoding='utf-8'))
            if state['status']!='completed_dru_bootstrap_not_photographic_qualification' or state['completed_steps']!=256:
                raise ValueError('All six complete arms required')
            states[arm,seed]=state
            for step in (0,128,256):
                path=path.parent/f'control-{step:06d}.json';value=json.loads(path.read_text(encoding='utf-8'))
                controls[arm,seed,step]=value
                result['records'].append(dict(arm=arm,seed=seed,step=step,source_sha256=sha256(path),groups=value['groups'],
                    minimum_gain=value['minimum_group_channel_gain'],
                    groups_below095=sum(min(g['mean_sensor_gain_by_colour'])<.95 for g in value['groups']),
                    median_group_mse_ratio=statistics.median(g['sensor_mse_ratio'] for g in value['groups'])))
    for seed in (71,73):
        fingerprints=[states[a,seed]['initial_model_fingerprint'] for a in ('reference','noise','details')]
        matching=len(set(fingerprints))==1
        same_control=all(controls[a,seed,0]==controls['reference',seed,0] for a in ('noise','details'))
        result['initialization_checks'].append(dict(seed=seed,initial_models_bit_exact=matching,initial_controls_equal=same_control))
        if not matching or not same_control:raise ValueError('Pairing contract failed')
        for arm in ('noise','details'):
            for step in (128,256):
                prior=controls['reference',seed,step]['groups'];candidate=controls[arm,seed,step]['groups']
                if len(prior)!=len(candidate):raise ValueError('Incomplete group pairing')
                for a,b in zip(prior,candidate):
                    keys=('frequency','chromatic','noise_index','fields')
                    if any(a[k]!=b[k] for k in keys):raise ValueError('Different control grouping')
                    result['paired_effects'].append(dict(arm=arm,seed=seed,step=step,**{k:a[k] for k in keys},
                        mse_ratio_delta=b['sensor_mse_ratio']-a['sensor_mse_ratio'],
                        gain_deltas=[v-u for u,v in zip(a['mean_sensor_gain_by_colour'],b['mean_sensor_gain_by_colour'])]))
    result['status']='completed_matched_data_comparison_known_only'
    path=campaign/'assessment.json'
    with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-data-comparison-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);run(p.parse_args().campaign)
