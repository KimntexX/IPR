"""Fixed enriched-model detail/colour evaluation; reuse frozen old thresholds."""
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
from neural_transfer_metrics import core_mask,error_sums,gradient_sums,summarize


def identities(rows):
    return sorted((r['id'],r['source_sha256'],r['content_sha256'],r['target_sha256']) for r in rows)


def load_pilot(parent,result):
    training=result['training'];freeze=result['freeze']
    if training is None or training['config']['steps']!=2048:raise ValueError('Expected completed fixed 2048-step pilot')
    for name,key in (('freeze.json','freeze_sha256'),('pairs.json','pairs_sha256'),('dataset.npz',None)):
        expected=(training[key] if key else result['qualification']['dataset']['sha256'])
        if digest(parent/name)!=expected:raise ValueError('Changed pilot inputs: '+name)
    if training['config']!=freeze['training']:raise ValueError('Training configuration mismatch')
    if digest(parent/'training.json')!=result['training_sha256']:raise ValueError('Changed training proof')
    weights=parent/'pilot-weights.pt'
    if digest(weights)!=training['weights_sha256']:raise ValueError('Changed weights')
    checkpoint=torch.load(weights,map_location='cpu',weights_only=True)
    if (checkpoint['width']!=training['config']['width'] or checkpoint['freeze_sha256']!=training['freeze_sha256']
            or checkpoint['pairs_sha256']!=training['pairs_sha256']):
        raise ValueError('Checkpoint provenance mismatch')
    model=CameraUNet(checkpoint['width']);model.load_state_dict(checkpoint['state_dict'],strict=True)
    model.requires_grad_(False);model.eval()
    return model,weights


def run(parent,baseline,output):
    root=Path(__file__).resolve().parents[2]
    parent,baseline,output=(p.resolve() for p in (parent,baseline,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    result_parent=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    old=json.loads(baseline.read_text(encoding='utf-8'))
    pairs=json.loads((parent/'pairs.json').read_text(encoding='utf-8'))
    validate_splits(pairs['examples'])
    indices=[i for i,r in enumerate(pairs['examples']) if r['split']=='validation']
    records=[pairs['examples'][i] for i in indices]
    if len(records)!=186 or identities(records)!=identities(old['records']):
        raise ValueError('Previously measured control source/target identities changed')
    for name in ('neural_transfer_metrics.py','camera_unet.py','camera_data.py'):
        if digest(Path(__file__).with_name(name))!=old['freeze']['tools'][name]:
            raise ValueError('Previous metric/model implementation changed')
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    model,weights=load_pilot(parent,result_parent)
    with np.load(parent/'dataset.npz',allow_pickle=False) as data:
        arrays={n:torch.from_numpy(data[n][indices].copy()) for n in ('signal','target','valid','sigma')}
    now=datetime.now(timezone.utc);campaign='neural-enriched-transfer-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),result_sha256=digest(parent/'result.json')),
        baseline=dict(path=str(baseline),sha256=digest(baseline)),weights_sha256=digest(weights),
        amount=100,scale=result_parent['freeze']['training']['scale'],
        structure_thresholds_from_original_training_only=old['freeze']['structure_thresholds_from_training_only'],
        selection_policy='One final checkpoint, original metrics/thresholds/control identities; no optimization or strength search.',
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('evaluate_enriched_transfer.py','neural_transfer_metrics.py','camera_unet.py','camera_data.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    started=time.monotonic();rows=[]
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,status='incomplete',
        versions=dict(torch=torch.__version__,numpy=np.__version__),threads_intra=threads,
        limits=['Previously examined single control composition, not independent final test.',
            'Camera RGB opponent error is not perceptual colour error; green gradient is not display luminance.',
            'Reference noise and alignment bias gradient projection; gains do not directly measure detail retained.',
            'Correlated patches, noisy burst averages, no visual or production qualification.'])
    try:
        with torch.inference_mode():
            for i,record in enumerate(records):
                source,target,valid,sigma=(arrays[n][i:i+1] for n in ('signal','target','valid','sigma'))
                for array,key in ((source,'content_sha256'),(target,'target_sha256')):
                    if hashlib.sha256(array.numpy().tobytes()).hexdigest()!=record[key]:raise ValueError('Changed control pixels')
                x,_,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),freeze['scale'])
                predicted=apply(model,x,s,100)*freeze['scale']
                if not torch.equal(apply(model,x,s,0),x):raise ValueError('Zero amount not identity')
                rows.append(dict(**record,full=error_sums(source,target,predicted,valid),
                    core24=error_sums(source,target,predicted,core_mask(valid)),
                    structure=gradient_sums(source,target,predicted,valid,freeze['structure_thresholds_from_original_training_only'])))
        result['summary']=summarize(rows)
        result['by_iso']={str(iso):summarize([r for r in rows if r['iso']==iso]) for iso in sorted({r['iso'] for r in rows})}
        result['previous_model_summary']=old['summary']
        if digest(weights)!=freeze['weights_sha256']:raise ValueError('Model changed during evaluation')
        result['status']='completed_fixed_enriched_transfer'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','baseline','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.baseline,a.output)
