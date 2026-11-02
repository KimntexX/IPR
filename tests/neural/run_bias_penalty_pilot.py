"""One fixed unit-weight channel-mean penalty experiment, no parameter search."""
import argparse
from copy import deepcopy
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import numpy as np
import torch

from camera_data import normalize_pair,validate_splits
from camera_unet import apply
from diagnose_train_learnability import digest,save
from evaluate_context_ablation import score
from evaluate_enriched_transfer import load_pilot
from evaluate_train_control import input_bias_sums,grouped_summaries
from neural_transfer_metrics import core_mask


def run(parent,baseline,output):
    root=Path(__file__).resolve().parents[2]
    parent,baseline,output=(p.resolve() for p in (parent,baseline,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    old=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    comparison=json.loads(baseline.read_text(encoding='utf-8'))
    if comparison['status']!='completed_fixed_train_control_diagnostics':raise ValueError('Expected train/control baseline')
    if digest(parent/'result.json')!=comparison['freeze']['parent']['result_sha256']:raise ValueError('Different baseline pilot')
    for n in ('camera_unet.py','camera_data.py','neural_transfer_metrics.py'):
        if digest(Path(__file__).with_name(n))!=comparison['freeze']['tools'][n]:raise ValueError('Metric/model code changed')
    if old['freeze']['training']['steps']!=2048 or old['freeze']['training'].get('channel_bias_weight',0)!=0:
        raise ValueError('Expected fixed unregularized baseline')
    dataset_hash=digest(parent/'dataset.npz')
    if dataset_hash!=old['qualification']['dataset']['sha256']:raise ValueError('Changed baseline dataset')
    if shutil.disk_usage(root).free<2*2**30:raise ValueError('Need 2 GiB reserve')
    now=datetime.now(timezone.utc);campaign='neural-bias-penalty-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    config=dict(old['freeze']['training'],channel_bias_weight=1.,channel_bias_border=24)
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),result_sha256=digest(parent/'result.json'),dataset_sha256=dataset_hash),
        baseline=dict(path=str(baseline),sha256=digest(baseline)),training=config,
        scene_partition=old['freeze']['scene_partition'],sources=old['freeze']['sources'],
        thresholds=comparison['freeze']['thresholds'],
        objective='Masked pixel MSE + 1 * mean over patch/channel of squared mean error on valid core24.',
        rationale='Unit weight adds one equal-unit mean-error term; no weight search or hard centering of outputs.',
        selection_policy='Same494 train/186 control, seed, optimizer, batch and 2048 steps; final checkpoint only.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('run_bias_penalty_pilot.py','train_photo_pilot.py',
            'photo_training_loss.py','evaluate_context_ablation.py','evaluate_enriched_transfer.py',
            'evaluate_train_control.py','neural_transfer_metrics.py','camera_unet.py','camera_data.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    pairs=deepcopy(old['qualification']);pairs['campaign_id']=campaign+'-pairs'
    pairs['freeze_sha256']=digest(output/'freeze.json')
    pairs['dataset']=dict(path=str(output/'dataset.npz'),sha256=dataset_hash)
    save(output/'pairs.json',pairs)
    started=time.monotonic();rows=[]
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),qualification=pairs,training=None,records=rows,status='incomplete',
        limits=['Previously examined control, no independent final test or production qualification.',
            'Single fixed mean-error weight, not an optimal hyperparameter claim.',
            'Mean targets are noisy; centering errors may reflect physical signal differences or registration.',
            'A lower bias does not prove improved chroma, preserved structure or visual quality.',
            'No gradient loss, extra scenes, synthetic-noise injection or architecture change.'])
    try:
        shutil.copyfile(parent/'dataset.npz',output/'dataset.npz')
        if digest(output/'dataset.npz')!=dataset_hash:raise ValueError('Dataset copy differs')
        validate_splits(pairs['examples'])
        print('START fixed 2048 steps, channel mean penalty weight1, core24',flush=True)
        env=dict(os.environ,OMP_NUM_THREADS='4',OPENBLAS_NUM_THREADS='2')
        with (output/'training.log').open('x',encoding='utf-8') as log:
            subprocess.run([sys.executable,str(root/'tests/neural/train_photo_pilot.py'),'--work',str(output)],
                cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
        result['training']=json.loads((output/'training.json').read_text(encoding='utf-8'))
        result['training_sha256']=digest(output/'training.json')
        threads=min(4,max(1,(os.cpu_count() or 1)-2))
        torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
        model,weights=load_pilot(output,result)
        with np.load(output/'dataset.npz',allow_pickle=False) as data:
            arrays={n:torch.from_numpy(data[n].copy()) for n in ('signal','target','valid','sigma')}
        with torch.inference_mode():
            for i,record in enumerate(pairs['examples']):
                source,target,valid,sigma=(arrays[n][i:i+1] for n in ('signal','target','valid','sigma'))
                for array,key in ((source,'content_sha256'),(target,'target_sha256')):
                    if hashlib.sha256(array.numpy().tobytes()).hexdigest()!=record[key]:raise ValueError('Changed sample pixels')
                x,_,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),config['scale'])
                predicted=apply(model,x,s,100)*config['scale']
                row=score(record,source,target,predicted,valid,freeze['thresholds'])
                row['full']['input_channel_bias_sum']=input_bias_sums(source,target,valid)
                row['core24']['input_channel_bias_sum']=input_bias_sums(source,target,core_mask(valid))
                rows.append(row)
        result['by_scene']=grouped_summaries(rows,'scene_group')
        result['by_split']=grouped_summaries(rows,'split')
        result['by_iso']=grouped_summaries(rows,'iso')
        result['baseline_by_scene']=comparison['by_scene'];result['baseline_by_iso']=comparison['by_iso']
        if digest(parent/'dataset.npz')!=dataset_hash:raise ValueError('Original dataset changed')
        if digest(weights)!=result['training']['weights_sha256']:raise ValueError('Weights changed')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Experiment code changed')
        result['status']='completed_fixed_bias_penalty_experiment'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['by_split'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','baseline','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.baseline,a.output)
