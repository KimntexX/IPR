"""Training-only learnability diagnostics; never score the reserved composition.

Fixed offset, fresh synthetic noise, one-pair memorization and longer real-data
training distinguish plumbing/optimization from real-pair difficulty. No model
selection, production changes, validation inference or new camera extraction.
"""
import argparse
from copy import deepcopy
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import numpy as np
import torch
from torch.nn import functional as F

from camera_data import normalize_pair,masked_mse,validate_splits
from camera_unet import CameraUNet,apply


def digest(path):
    value=hashlib.sha256()
    with Path(path).open('rb') as handle:
        for part in iter(lambda:handle.read(1024*1024),b''):value.update(part)
    return value.hexdigest()


def save(path,value):
    with path.open('x',encoding='utf-8') as handle:
        json.dump(value,handle,indent=2,allow_nan=False)


def training_data(work):
    freeze=json.loads((work/'freeze.json').read_text(encoding='utf-8'))
    pairs=json.loads((work/'pairs.json').read_text(encoding='utf-8'))
    if (digest(work/'freeze.json')!=pairs['freeze_sha256'] or
            digest(work/'dataset.npz')!=pairs['dataset']['sha256']):
        raise ValueError('Changed parent freeze or dataset')
    validate_splits(pairs['examples'])
    indices=[i for i,r in enumerate(pairs['examples']) if r['split']=='train']
    if not indices:raise ValueError('No training examples')
    records=[deepcopy(pairs['examples'][i]) for i in indices]
    # The shared archive contains both partitions, but only training rows become
    # tensors or enter any metric/optimization in this diagnostic.
    with np.load(work/'dataset.npz',allow_pickle=False) as data:
        arrays={name:torch.from_numpy(data[name][indices].copy())
                for name in ('signal','target','valid','sigma')}
    for i,row in enumerate(records):
        for field,key in (('signal','content_sha256'),('target','target_sha256')):
            if hashlib.sha256(arrays[field][i].numpy().tobytes()).hexdigest()!=row[key]:
                raise ValueError('Training example identity mismatch')
    if not all(torch.isfinite(value).all() for value in arrays.values()):
        raise ValueError('Nonfinite training data')
    if arrays['valid'].dtype!=torch.bool or not arrays['valid'].any():
        raise ValueError('Empty or non-Boolean training masks')
    return freeze,records,arrays


def measure(model,signal,target,valid,sigma,scale):
    model.eval();input_sum=0.;output_sum=0.;count=0
    for first in range(0,len(signal),8):
        x=signal[first:first+8];y=target[first:first+8];mask=valid[first:first+8]
        s=sigma[first:first+8,:,None,None].expand_as(x)
        normalized,_,conditioning=normalize_pair(x,y,s,scale)
        output=apply(model,normalized,conditioning,100)*scale
        samples=int(mask.sum())*3
        input_sum+=float(masked_mse(x,y,mask))*samples
        output_sum+=float(masked_mse(output,y,mask))*samples;count+=samples
    if not count:raise ValueError('No measurable training samples')
    return dict(input_mse=input_sum/count,output_mse=output_sum/count,
                mse_ratio=output_sum/input_sum if input_sum else None,
                valid_channel_samples=count)


def fit_case(name,arrays,config,steps,checkpoints,work,fresh_noise=False):
    torch.manual_seed(config['seed'])
    model=CameraUNet(width=config['width'])
    optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
    signal,target,valid,sigma=(arrays[k] for k in ('signal','target','valid','sigma'))
    generator=torch.Generator().manual_seed(config['seed']+100)
    recorded=[dict(step=0,**measure(model,signal,target,valid,sigma,config['scale']))]
    losses=[];gradients=[];started=time.monotonic()
    for step in range(steps):
        indices=torch.randint(len(signal),(config['batch'],))
        x=signal[indices];y=target[indices];s=sigma[indices]
        if fresh_noise:x=y+torch.randn(y.shape,generator=generator)*s[:,:,None,None]
        x,y,s=normalize_pair(x,y,s[:,:,None,None].expand_as(x),config['scale'])
        model.train();optimizer.zero_grad(set_to_none=True)
        loss=masked_mse(model(x,s),y,valid[indices]);loss.backward()
        if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
            raise ValueError('Nonfinite gradient')
        if step in (0,1):
            norms={part:float(torch.sqrt(sum((p.grad.square().sum() for n,p in model.named_parameters()
                if p.grad is not None and n.startswith('head')==(part=='head')),torch.tensor(0.))))
                for part in ('head','body')}
            gradients.append(dict(step=step,**norms))
        optimizer.step();losses.append(float(loss.detach().item()))
        if step+1 in checkpoints:
            row=dict(step=step+1,**measure(model,signal,target,valid,sigma,config['scale']))
            recorded.append(row)
            print(f'{name} step={step+1} train-only MSE ratio={row["mse_ratio"]:.6g}',flush=True)
    destination=work/(name+'-diagnostic-weights.pt')
    torch.save(dict(state_dict=model.state_dict(),width=config['width'],diagnostic=name,
                    partition='train_only_not_for_production'),destination)
    return dict(name=name,steps=steps,checkpoint_metrics=recorded,losses=losses,
                first_gradients=gradients,elapsed_seconds=time.monotonic()-started,
                weights_sha256=digest(destination),fresh_noise_per_update=fresh_noise)


def residual_statistics(records,arrays):
    rows=[]
    for iso in sorted({r['iso'] for r in records}):
        indices=[i for i,r in enumerate(records) if r['iso']==iso]
        delta=arrays['signal'][indices]-arrays['target'][indices]
        mask=arrays['valid'][indices].expand_as(delta)
        pooled=F.avg_pool2d(F.pad(delta,(4,4,4,4),mode='reflect'),9,stride=1)
        mse=float(delta.square()[mask].mean())
        means=[float(delta[:,c][mask[:,c]].mean()) for c in range(3)]
        rows.append(dict(iso=iso,patches=len(indices),valid_channel_samples=int(mask.sum()),
            residual_mse=mse,channel_means=means,
            mean_square_channel_bias=sum(x*x for x in means)/3,
            local_mean_9x9_energy_fraction=float(pooled.square()[mask].mean())/mse if mse else None))
    return rows


def run(parent,output):
    root=Path(__file__).resolve().parents[2];parent=parent.resolve();output=output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use the dedicated neural build')
    parent_freeze,records,arrays=training_data(parent)
    config=deepcopy(parent_freeze['training'])
    now=datetime.now(timezone.utc);campaign='neural-train-learnability-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    fixed_indices=np.unique(np.linspace(0,len(records)-1,min(4,len(records)),dtype=int)).tolist()
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),freeze_sha256=digest(parent/'freeze.json'),
                    pairs_sha256=digest(parent/'pairs.json'),dataset_sha256=digest(parent/'dataset.npz')),
        config=config,cases=dict(known_offset=256,fresh_synthetic_noise=1024,
                                one_real_pair=1024,real_training_long=2048),
        checkpoints=[256,1024,2048],selected_training_indices=fixed_indices,
        synthetic_noise_sigma=.02,offset_rgb=[.02,-.015,.01],
        noise_seeds=dict(training=config['seed']+100,evaluation=config['seed']+200),
        tools={name:digest(Path(__file__).with_name(name)) for name in
               ('diagnose_train_learnability.py','camera_unet.py','camera_data.py')},
        records=records,partition_policy='Training rows only; no reserved-control metric or inference.')
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),cases=[],status='incomplete',
        threads_intra=threads,threads_inter=1,versions=dict(torch=torch.__version__,numpy=np.__version__),
        limits=['Only training composition and ISOs; no validation of generalization.',
                'Known injected Gaussian noise is not real signal-dependent correlated sensor noise.',
                'Burst-average targets retain noise and possible alignment/radiometry errors.',
                'One-pair memorization and train MSE are not denoising quality.',
                'Longer training is diagnostic, not selected or deployed using reserved-control outcomes.'])
    started=time.monotonic()
    try:
        result['residual_statistics']=residual_statistics(records,arrays)
        small={k:v[fixed_indices].clone() for k,v in arrays.items()}
        target=small['target']
        small['signal']=target+torch.tensor(freeze['offset_rgb'])[None,:,None,None]
        small['sigma']=torch.full_like(small['sigma'],.02)
        result['cases'].append(fit_case('known_offset',small,config,256,[256],output))
        noise=torch.randn(target.shape,generator=torch.Generator().manual_seed(config['seed']+200))*.02
        small['signal']=target+noise
        result['cases'].append(fit_case('fresh_synthetic_noise',small,config,1024,[256,1024],output,True))
        single={k:v[:1].clone() for k,v in arrays.items()}
        result['cases'].append(fit_case('one_real_pair',single,config,1024,[256,1024],output))
        result['cases'].append(fit_case('real_training_long',arrays,config,2048,[256,1024,2048],output))
        if digest(parent/'dataset.npz')!=freeze['parent']['dataset_sha256']:
            raise ValueError('Parent dataset changed during diagnosis')
        if any(digest(Path(__file__).with_name(name))!=value for name,value in freeze['tools'].items()):
            raise ValueError('Diagnostic code changed during execution')
        result['status']='completed_training_only_diagnostics'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(args.parent,args.output)
