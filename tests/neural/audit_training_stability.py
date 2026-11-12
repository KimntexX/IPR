"""Train-only exact replays with fixed full-train probes; no model selection."""
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

from camera_data import normalize_pair
from camera_unet import CameraUNet,apply
from diagnose_train_learnability import digest,save
from neural_transfer_metrics import core_mask
from photo_training_loss import objective


PROBES=(0,512,1024,1536,1792,1920,2048)


def state_hash(state):
    result=hashlib.sha256()
    for key in sorted(state):
        result.update(key.encode('utf-8'));result.update(state[key].detach().cpu().contiguous().numpy().tobytes())
    return result.hexdigest()


def summarize_probe(rows,scale,weight):
    if not rows or scale<=0:raise ValueError('Nonempty rows and positive scale required')
    count=sum(r['pixels'] for r in rows)
    if count<=0:raise ValueError('Empty probe support')
    mse=sum(r['sse'] for r in rows)/(3*count)
    bias=sum(r['mean_square_bias'] for r in rows)/len(rows)
    return dict(patches=len(rows),pixels=count,camera_mse=mse,camera_mean_square_bias=bias,
        normalized_objective=(mse+weight*bias)/(scale*scale),
        camera_channel_bias=[sum(r['bias_sum'][c] for r in rows)/count for c in range(3)])


def full_train_probe(model,arrays,records,config):
    rng_before=torch.get_rng_state().clone();model.eval();rows=[]
    with torch.inference_mode():
        for start in range(0,len(records),8):
            source,target,valid,sigma=(arrays[n][start:start+8] for n in ('signal','target','valid','sigma'))
            x,_,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),config['scale'])
            output=apply(model,x,s,100)*config['scale'];error=output-target;core=core_mask(valid)
            for i in range(len(source)):
                mask=valid[i,0];inner=core[i,0];count=int(mask.sum())
                if not inner.any():raise ValueError('Empty training interior')
                means=torch.stack([error[i,c][inner].mean() for c in range(3)])
                rows.append(dict(scene=records[start+i]['scene_group'],pixels=count,
                    sse=float(error[i][:,mask].square().sum()),mean_square_bias=float(means.square().mean()),
                    bias_sum=[float(error[i,c][mask].sum()) for c in range(3)]))
    if not torch.equal(torch.get_rng_state(),rng_before):raise ValueError('Probe consumed training RNG')
    model.train()
    return dict(all=summarize_probe(rows,config['scale'],config.get('channel_bias_weight',0.)),
        scenes={scene:summarize_probe([r for r in rows if r['scene']==scene],config['scale'],config.get('channel_bias_weight',0.))
            for scene in sorted({r['scene'] for r in rows})})


def content_balance(records,measured):
    rows={r['id']:r for r in measured}
    if set(rows)!={r['id'] for r in records}:raise ValueError('Training evidence coverage mismatch')
    total=sum(rows[r['id']]['full']['input_sse'] for r in records)
    result={}
    for scene in sorted({r['scene_group'] for r in records}):
        selected=[r for r in records if r['scene_group']==scene]
        result[scene]=dict(patches=len(selected),raws=len({r['source_sha256'] for r in selected}),
            patch_sampling_fraction=len(selected)/len(records),
            input_sse_fraction=sum(rows[r['id']]['full']['input_sse'] for r in selected)/total if total else None,
            gradient_components={band:sum(rows[r['id']]['structure'][band]['components'] for r in selected)
                for band in ('low','weak','strong')})
    return result


def run(parent,penalty,balance,output):
    root=Path(__file__).resolve().parents[2]
    parent,penalty,balance,output=(p.resolve() for p in (parent,penalty,balance,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,penalty,output)):
        raise ValueError('Use dedicated neural build')
    original=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    variant=json.loads((penalty/'result.json').read_text(encoding='utf-8'))
    evidence=json.loads(balance.read_text(encoding='utf-8'))
    if digest(parent/'result.json')!=evidence['freeze']['parent']['result_sha256']:
        raise ValueError('Wrong train evidence parent')
    if variant['status']!='completed_fixed_bias_penalty_experiment':raise ValueError('Incomplete variant')
    if digest(parent/'result.json')!=variant['freeze']['parent']['result_sha256']:raise ValueError('Different variant parent')
    data_hash=digest(parent/'dataset.npz')
    if data_hash!=original['qualification']['dataset']['sha256'] or data_hash!=variant['freeze']['parent']['dataset_sha256']:
        raise ValueError('Changed training data')
    indices=[i for i,r in enumerate(original['qualification']['examples']) if r['split']=='train']
    records=[original['qualification']['examples'][i] for i in indices]
    if len(records)!=494:raise ValueError('Unexpected train coverage')
    with np.load(parent/'dataset.npz',allow_pickle=False) as data:
        arrays={n:torch.from_numpy(data[n][indices].copy()) for n in ('signal','target','valid','sigma')}
    for i,r in enumerate(records):
        for name,key in (('signal','content_sha256'),('target','target_sha256')):
            if hashlib.sha256(arrays[name][i].numpy().tobytes()).hexdigest()!=r[key]:raise ValueError('Changed train pixels')
    cases=[('pixel_mse',parent,original),('unit_mean_penalty',penalty,variant)]
    now=datetime.now(timezone.utc);campaign='neural-training-stability-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={name:dict(path=str(p),result_sha256=digest(p/'result.json')) for name,p,_ in cases},
        balance_evidence=dict(path=str(balance),sha256=digest(balance)),dataset_sha256=data_hash,
        probes=list(PROBES),terminal_window=[1792,1920,2048],training_examples=records,
        configurations={name:proof['training']['config'] for name,_,proof in cases},
        policy='Train only; all494 fixed training patches at each probe; no checkpoint selection, continuation or new recipe.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('audit_training_stability.py','photo_training_loss.py',
            'camera_data.py','camera_unet.py','neural_transfer_metrics.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),content_balance=content_balance(records,
            [r for r in evidence['records'] if r['split']=='train']),cases=[],status='incomplete',
        threads_intra=threads,versions=dict(torch=torch.__version__,numpy=np.__version__),
        limits=['Train-only replays are not new independent evidence of generalization.',
            'Input SSE includes noise, mismatch and texture; not a direct gradient-contribution measurement.',
            'Full-train aggregate objective is not exactly the expectation of randomly masked minibatch losses.',
            'Fixed terminal-window variability diagnoses behavior, not proof of global convergence.',
            'No validation pixel evaluation, recipe search, production change or new candidate weights.'])
    started=time.monotonic()
    try:
        for name,folder,proof in cases:
            config=proof['training']['config'];weights=folder/'pilot-weights.pt'
            if config['steps']!=2048 or digest(weights)!=proof['training']['weights_sha256']:raise ValueError('Changed fixed model')
            reference=torch.load(weights,map_location='cpu',weights_only=True)['state_dict']
            torch.manual_seed(config['seed']);model=CameraUNet(config['width'])
            optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
            case=dict(name=name,probes=[],sampled_batches=[],initial_state_sha256=state_hash(model.state_dict()),
                expected_state_sha256=state_hash(reference),expected_weights_sha256=digest(weights))
            result['cases'].append(case)
            case['probes'].append(dict(step=0,**full_train_probe(model,arrays,records,config)))
            for step in range(1,2049):
                sample=torch.randint(len(records),(config['batch'],))
                case['sampled_batches'].append(sample.tolist())
                source,target,valid,sigma=(arrays[n][sample] for n in ('signal','target','valid','sigma'))
                x,y,s=normalize_pair(source,target,sigma[:,:,None,None].expand_as(source),config['scale'])
                model.train();optimizer.zero_grad(set_to_none=True)
                loss,_,_=objective(model(x,s),y,valid,config.get('channel_bias_weight',0.),config.get('channel_bias_border',24))
                loss.backward()
                if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):raise ValueError('Nonfinite gradient')
                optimizer.step()
                if step in PROBES:
                    case['probes'].append(dict(step=step,**full_train_probe(model,arrays,records,config)))
                    print(f'PROBE {name} {step}/2048',flush=True)
            case['final_state_sha256']=state_hash(model.state_dict())
            case['exact_final_tensor_match']=all(torch.equal(value,reference[key]) for key,value in model.state_dict().items())
            if not case['exact_final_tensor_match']:raise ValueError('Replay does not reproduce original weights')
            tail=[p['all']['normalized_objective'] for p in case['probes'] if p['step'] in freeze['terminal_window']]
            case['terminal_objective_range_over_mean']=(max(tail)-min(tail))/(sum(tail)/len(tail))
            case['final_objective_relative_to_step1792']=tail[-1]/tail[0] if tail[0] else None
        if result['cases'][0]['sampled_batches']!=result['cases'][1]['sampled_batches']:
            raise ValueError('Recipes did not use identical sampled batches')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Code changed')
        result['status']='completed_train_only_exact_stability_replays'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','penalty','balance','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.penalty,a.balance,a.output)
