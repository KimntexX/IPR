"""Frozen-model train/control diagnostics on identical interior metrics, no fitting."""
import argparse
from collections import defaultdict
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import numpy as np
import torch

from camera_data import normalize_pair,validate_splits
from camera_unet import apply
from diagnose_train_learnability import digest,save
from evaluate_context_ablation import score
from evaluate_enriched_transfer import identities,load_pilot
from neural_transfer_metrics import core_mask,summarize


def input_bias_sums(source,target,valid):
    if source.shape!=target.shape or valid.shape!=(source.shape[0],1,*source.shape[2:]) or valid.dtype!=torch.bool:
        raise ValueError('Matching RGB tensors and Boolean mask required')
    if not torch.isfinite(source).all() or not torch.isfinite(target).all():raise ValueError('Nonfinite bias input')
    return [float((source-target)[:,c][valid[:,0]].sum()) for c in range(3)]


def summarize_with_bias(rows):
    result=summarize(rows)
    for region in ('full','core24'):
        pixels=result[region]['pixels']
        if not pixels:continue
        before=[sum(r[region]['input_channel_bias_sum'][c] for r in rows)/pixels for c in range(3)]
        result[region]['input_camera_channel_bias']=before
        result[region]['output_minus_input_camera_bias']=[a-b for a,b in zip(result[region]['camera_channel_bias'],before)]
    return result


def grouped_summaries(rows,key):
    groups=defaultdict(list)
    for row in rows:groups[str(row[key])].append(row)
    result={}
    for name,group in groups.items():
        result[name]=dict(patches=len(group),unique_source_raws=len({r['source_sha256'] for r in group}),
            splits=sorted({r['split'] for r in group}),isos=sorted({r['iso'] for r in group}),
            metrics=summarize_with_bias(group))
    return result


def run(parent,baseline,output):
    root=Path(__file__).resolve().parents[2]
    parent,baseline,output=(p.resolve() for p in (parent,baseline,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    pilot=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    old=json.loads(baseline.read_text(encoding='utf-8'))
    if old['status']!='completed_fixed_enriched_transfer':raise ValueError('Expected completed fixed evaluation')
    if digest(parent/'result.json')!=old['freeze']['parent']['result_sha256']:raise ValueError('Different model campaign')
    for n in ('neural_transfer_metrics.py','camera_unet.py','camera_data.py'):
        if digest(Path(__file__).with_name(n))!=old['freeze']['tools'][n]:raise ValueError('Changed metric/model implementation')
    pairs=json.loads((parent/'pairs.json').read_text(encoding='utf-8'))
    validate_splits(pairs['examples'])
    if len(pairs['examples'])!=680 or identities([r for r in pairs['examples'] if r['split']=='validation'])!=identities(old['records']):
        raise ValueError('Changed frozen train/control coverage')
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    model,weights=load_pilot(parent,pilot)
    with np.load(parent/'dataset.npz',allow_pickle=False) as data:
        arrays={n:torch.from_numpy(data[n].copy()) for n in ('signal','target','valid','sigma')}
    now=datetime.now(timezone.utc);campaign='neural-train-control-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),result_sha256=digest(parent/'result.json')),
        baseline=dict(path=str(baseline),sha256=digest(baseline)),weights_sha256=digest(weights),
        amount=100,scale=old['freeze']['scale'],thresholds=old['freeze']['structure_thresholds_from_original_training_only'],
        measurement='Same96px inference, fixed core24 metrics known insensitive to context384 on this control.',
        selection_policy='All680 frozen patches, no training, model/strength search or new thresholds.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('evaluate_train_control.py','evaluate_context_ablation.py',
            'evaluate_enriched_transfer.py','neural_transfer_metrics.py','camera_unet.py','camera_data.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    started=time.monotonic();rows=[]
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,status='incomplete',
        threads_intra=threads,versions=dict(torch=torch.__version__,numpy=np.__version__),
        limits=['Training metrics are in-sample; previously consulted control is not independent final test.',
            'Two train compositions and one control composition with different ISO/scene/radiometry distributions.',
            'Observed differences cannot uniquely distinguish loss, target bias, conditioning, capacity or optimization.',
            'Reference noise and registration affect gradient projection; not percentage of true detail retained.',
            'Camera opponent/green measures are not perceptual colour/display luminance.',
            'No new training, capture, display render or production change.'])
    try:
        with torch.inference_mode():
            for i,record in enumerate(pairs['examples']):
                source,target,valid,sigma=(arrays[n][i:i+1] for n in ('signal','target','valid','sigma'))
                for array,key in ((source,'content_sha256'),(target,'target_sha256')):
                    if hashlib.sha256(array.numpy().tobytes()).hexdigest()!=record[key]:raise ValueError('Changed frozen pixels')
                x,_,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),freeze['scale'])
                predicted=apply(model,x,s,100)*freeze['scale']
                row=score(record,source,target,predicted,valid,freeze['thresholds'])
                row['full']['input_channel_bias_sum']=input_bias_sums(source,target,valid)
                row['core24']['input_channel_bias_sum']=input_bias_sums(source,target,core_mask(valid))
                rows.append(row)
        result['by_split']=grouped_summaries(rows,'split')
        result['by_scene']=grouped_summaries(rows,'scene_group')
        result['by_iso']=grouped_summaries(rows,'iso')
        control=result['by_split']['validation']['metrics']
        for region in ('full','core24'):
            for metric in ('mse_ratio','opponent_mse_ratio'):
                if not np.isclose(control[region][metric],old['summary'][region][metric],rtol=1e-6,atol=1e-9):
                    raise ValueError('Replayed control differs from frozen evaluation')
        if digest(weights)!=freeze['weights_sha256']:raise ValueError('Model changed during evaluation')
        result['status']='completed_fixed_train_control_diagnostics'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['by_scene'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','baseline','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.baseline,a.output)
