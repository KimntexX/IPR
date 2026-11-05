"""All existing strong fine fields, direct sensor-domain diagnostic, no new cohort."""
import json
from pathlib import Path
import torch
from torch.nn import functional as F
from bayer_canonical import canonicalize,spatial_reflect
from confirm_fine_noise import fine_signal,observation
from context_data import extend_observation
from run_noisy_moment_pilot import NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from sensor_transfer_math import site_response


def evaluate(model,root,check=lambda:None,limit=None):
    source=root/'build-patch_neural_restoration/curve-known-fine-v001/result.json'
    plan=json.loads(source.read_text(encoding='utf-8'))['plan']
    fields=[r for r in plan['fields'] if plan['cases'][r['case_index']]['amplitude']==.06 and r['noise_index']>0]
    if len(fields)!=864:raise ValueError('Expected all existing strong noisy fields')
    if limit is not None:fields=fields[:limit]
    rows=[];prior=model.training;model.eval()
    try:
        with torch.inference_mode():
            for field in fields:
                check();case=plan['cases'][field['case_index']]
                truth,wave,cosine=fine_signal(case)
                if tensor_hash(truth)!=case['target_sha256']:raise ValueError('Known clean field changed')
                tags=torch.tensor([PATTERNS[field['phase_index']]])
                clean,values,sigma=observation(truth,tags,field)
                if tensor_hash(values)!=field['observation_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:
                    raise ValueError('Known observed field changed')
                expanded,_,_=fine_signal(case,1024,-416)
                values,deviation=extend_observation(expanded,tags,values,sigma,*NOISE[field['noise_index']],210000000+field['noise_seed'])
                packed,deviations,labels,flags=canonicalize(values/2,deviation/2,tags)
                corrected=model.correct_observations(packed.cuda(),deviations.cuda(),labels.cuda()).cpu()
                restored=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(corrected,2),flags)*2,2)[:,:,208:304,208:304]
                _,observed,_=observation(truth,tags,field)
                rows.append(dict(**field,frequency=case['frequency'],chromatic=case['chromatic'],
                    angle=case['angle'],site_colours=tags[0].tolist(),
                    corrected=site_response(restored,clean,wave,cosine),
                    observed=site_response(observed,clean,wave,cosine)))
    finally:model.train(prior)
    groups=[]
    for frequency in sorted({r['frequency'] for r in rows}):
        for chromatic in (False,True):
            for level in (1,2):
                selected=[r for r in rows if r['frequency']==frequency and r['chromatic']==chromatic and r['noise_index']==level]
                if not selected:continue
                gains=[]
                for colour in range(4):
                    gains.append(sum(r['corrected'][r['site_colours'].index(colour)]['sine_gain'] for r in selected)/len(selected))
                mse=sum(sum(s['mse'] for s in r['corrected']) for r in selected)
                baseline=sum(sum(s['mse'] for s in r['observed']) for r in selected)
                groups.append(dict(frequency=frequency,chromatic=chromatic,noise_index=level,fields=len(selected),
                    mean_sensor_gain_by_colour=gains,sensor_mse_ratio=mse/max(baseline,1e-20)))
    return dict(scope='Known sensor diagnostics only; no legacy learned RGB parent, no new reserve or photographic qualification.',
        observations=len(rows),records=rows,groups=groups,
        minimum_group_channel_gain=min(g for row in groups for g in row['mean_sensor_gain_by_colour']),
        strong_sensor_transfer_at_least095=all(g>=.95 for row in groups for g in row['mean_sensor_gain_by_colour']))
