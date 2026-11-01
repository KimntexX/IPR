"""One fixed evaluation of the 2048-step training-only model; no optimization."""
import argparse
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
from camera_unet import CameraUNet,apply
from diagnose_train_learnability import digest,save
from neural_transfer_metrics import structure_thresholds,core_mask,error_sums,gradient_sums,summarize


def load_frozen_model(diagnostic,proof):
    cases=[r for r in proof['cases'] if r['name']=='real_training_long']
    if proof['status']!='completed_training_only_diagnostics' or len(cases)!=1 or cases[0]['steps']!=2048:
        raise ValueError('Expected the fixed, completed 2048-step training-only model')
    weights=diagnostic/'real_training_long-diagnostic-weights.pt'
    if digest(weights)!=cases[0]['weights_sha256']:raise ValueError('Changed model weights')
    checkpoint=torch.load(weights,map_location='cpu',weights_only=True)
    if checkpoint['diagnostic']!='real_training_long' or checkpoint['width']!=proof['freeze']['config']['width']:
        raise ValueError('Wrong model identity or width')
    model=CameraUNet(checkpoint['width']);model.load_state_dict(checkpoint['state_dict'],strict=True)
    model.requires_grad_(False);model.eval()
    return model,weights


def run(parent,diagnostic,output):
    root=Path(__file__).resolve().parents[2]
    parent,diagnostic,output=(p.resolve() for p in (parent,diagnostic,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,diagnostic,output)):
        raise ValueError('Use the dedicated neural build')
    proof=json.loads((diagnostic/'result.json').read_text(encoding='utf-8'))
    pairs=json.loads((parent/'pairs.json').read_text(encoding='utf-8'))
    for name,key in (('dataset.npz','dataset_sha256'),('pairs.json','pairs_sha256'),('freeze.json','freeze_sha256')):
        if digest(parent/name)!=proof['freeze']['parent'][key]:raise ValueError('Changed pilot input')
    for name in ('camera_unet.py','camera_data.py'):
        if digest(Path(__file__).with_name(name))!=proof['freeze']['tools'][name]:
            raise ValueError('Changed model/normalization implementation')
    validate_splits(pairs['examples'])
    training=[i for i,r in enumerate(pairs['examples']) if r['split']=='train']
    validation=[i for i,r in enumerate(pairs['examples']) if r['split']=='validation']
    if not training or len(validation)!=186:raise ValueError('Unexpected frozen coverage')
    records=[pairs['examples'][i] for i in validation]
    if any(r['scene_group']!='control_indoor' for r in records):raise ValueError('Wrong reserved composition')
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    with np.load(parent/'dataset.npz',allow_pickle=False) as data:
        cutoffs=structure_thresholds(torch.from_numpy(data['target'][training]),torch.from_numpy(data['valid'][training]))
        arrays={name:torch.from_numpy(data[name][validation].copy()) for name in ('signal','target','valid','sigma')}
    for i,row in enumerate(records):
        for name,key in (('signal','content_sha256'),('target','target_sha256')):
            if hashlib.sha256(arrays[name][i].numpy().tobytes()).hexdigest()!=row[key]:
                raise ValueError('Changed reserved pixel identity')
    model,weights=load_frozen_model(diagnostic,proof)
    now=datetime.now(timezone.utc);campaign='neural-reserved-transfer-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=proof['freeze']['parent'],diagnostic_result_sha256=digest(diagnostic/'result.json'),
        weights_sha256=digest(weights),model_case='real_training_long',steps=2048,amount=100,
        scale=proof['freeze']['config']['scale'],structure_thresholds_from_training_only=cutoffs,
        metric_protocol='camera RGB MSE/bias, R-G and B-G opponent error, green-gradient projection; core24 excludes patch borders',
        tools={name:digest(Path(__file__).with_name(name)) for name in
               ('evaluate_reserved_transfer.py','neural_transfer_metrics.py','camera_unet.py','camera_data.py')},
        reference_policy='Original primary ISO100 burst only; fixed masks/identities, no alternate reference',
        selection_policy='No optimizer, retraining, blending search or checkpoint selection')
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    started=time.monotonic();rows=[]
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,status='incomplete',
        threads_intra=threads,threads_inter=1,versions=dict(torch=torch.__version__,numpy=np.__version__),
        limits=['One previously evaluated composition, not a pristine final test or product qualification.',
                'Training ISO12800/51200 versus control ISO1600/3200/6400; no matched-ISO generalization claim.',
                'Noisy averaged targets, correlated overlapping/capture samples; no perfectly clean truth.',
                'Camera RGB opponents/green are not perceptual colour differences or display luminance.',
                'Gradient projection is a detail proxy, affected by residual reference noise and registration.',
                'No full-resolution display render, visual validation or production integration.'])
    try:
        for i,record in enumerate(records):
            source,target,valid,sigma=(arrays[name][i:i+1] for name in ('signal','target','valid','sigma'))
            x,_,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),freeze['scale'])
            output_image=apply(model,x,s,100)*freeze['scale']
            if not torch.equal(apply(model,x,s,0),x):raise ValueError('Zero amount is not identity')
            if hashlib.sha256(source.numpy().tobytes()).hexdigest()!=record['content_sha256']:
                raise ValueError('Inference modified the source')
            rows.append(dict(**record,full=error_sums(source,target,output_image,valid),
                core24=error_sums(source,target,output_image,core_mask(valid)),
                structure=gradient_sums(source,target,output_image,valid,cutoffs),
                output_min=float(output_image.min()),output_max=float(output_image.max())))
        result['summary']=summarize(rows)
        result['by_iso']={str(iso):summarize([r for r in rows if r['iso']==iso]) for iso in sorted({r['iso'] for r in rows})}
        if digest(weights)!=freeze['weights_sha256']:raise ValueError('Weights changed during evaluation')
        result['status']='completed_fixed_model_transfer'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('parent','diagnostic','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();run(args.parent,args.diagnostic,args.output)
