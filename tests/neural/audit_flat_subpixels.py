"""Fixed-model flat-field residual decomposition, no correction or training."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

import torch
from torch.nn import functional as F

from bayer_canonical import canonicalize,predict,spatial_reflect
from diagnose_train_learnability import digest,save
from joint_bayer_unet import JointBayerUNet,sample_rgb,sparse_rgb
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS,signal
from audit_bayer_phases import PHASE_NAMES


def decompose(error,pattern):
    if (error.ndim!=4 or error.shape[0]!=1 or error.shape[1]!=3 or min(error.shape[2:])<2
            or any(v%2 for v in error.shape[2:]) or not torch.isfinite(error).all()):
        raise ValueError('Finite single RGB error with even dimensions required')
    if (pattern.shape!=(1,4) or pattern.dtype!=torch.int64 or pattern.device!=error.device
            or ((pattern<0)|(pattern>3)).any()):raise ValueError('Matching integer Bayer pattern required')
    tags=torch.where(pattern==3,1,pattern)
    if ((tags==0).sum()!=1) or ((tags==2).sum()!=1) or ((tags==1).sum()!=2):
        raise ValueError('One Bayer pattern required')
    observed=F.one_hot(tags,num_classes=3).permute(0,2,1).reshape(1,3,2,2)
    observed=observed.expand(1,3,2,2).repeat(1,1,error.shape[2]//2,error.shape[3]//2).bool()
    if error[observed].abs().max()!=0:raise ValueError('Expected exact observed sites, not noisy data')
    value=error.double();missing=~observed
    dc=value.mean(dim=(2,3),keepdim=True)
    means=torch.stack([value[:,:,y::2,x::2].mean(dim=(0,2,3)) for y,x in ((0,0),(0,1),(1,0),(1,1))])
    cells=means.T.reshape(1,3,2,2).repeat(1,1,value.shape[2]//2,value.shape[3]//2)
    within=value-cells
    missing_mean=(value*missing).sum(dim=(2,3),keepdim=True)/missing.sum(dim=(2,3),keepdim=True)
    missing_template=missing_mean*missing
    extra_periodic=cells-missing_template
    mse=float(value.square().mean());within_mse=float(within.square().mean())
    parts=dict(dc_mse=float(dc.square().mean()),periodic_2x2_excluding_dc_mse=float((cells-dc).square().mean()),
        within_cell_mse=within_mse,missing_channel_mean_mse=float(missing_template.square().mean()),
        extra_periodic_beyond_missing_channel_mean_mse=float(extra_periodic.square().mean()))
    if abs(sum(parts[n] for n in ('dc_mse','periodic_2x2_excluding_dc_mse','within_cell_mse'))-mse)>1e-10*max(mse,1e-12):
        raise ValueError('Cell decomposition energy mismatch')
    if abs(sum(parts[n] for n in ('missing_channel_mean_mse','extra_periodic_beyond_missing_channel_mean_mse','within_cell_mse'))-mse)>1e-10*max(mse,1e-12):
        raise ValueError('Missing-site decomposition energy mismatch')
    return dict(total_mse=mse,**parts,bias_rgb=dc.reshape(3).tolist(),
        mean_error_position_then_rgb=means.tolist(),missing_channel_bias_rgb=missing_mean.reshape(3).tolist(),
        cell_template_explained_fraction=1-within_mse/mse if mse else None,
        extra_subpixel_fraction=parts['extra_periodic_beyond_missing_channel_mean_mse']/mse if mse else None)


def head_bias_field(model,packed,sigma,pattern,config):
    canonical=config.get('canonical_input',False)
    if canonical:values,deviations,labels,flags=canonicalize(packed,sigma,pattern)
    else:values,deviations,labels=packed,sigma,pattern
    if (deviations!=0).any():raise ValueError('Noiseless algebraic diagnostic only')
    h,w=values.shape[2:]
    contribution=F.pixel_shuffle(model.head.bias.detach()[None,:,None,None].expand(1,12,h,w),2)
    _,observed=sparse_rgb(values,labels)
    contribution=contribution*(observed==0)*config['scale']
    return spatial_reflect(contribution,flags) if canonical else contribution


def summarize(rows):
    if not rows:raise ValueError('Nonempty flat-field diagnostics required')
    keys=('total_mse','dc_mse','periodic_2x2_excluding_dc_mse','within_cell_mse',
        'missing_channel_mean_mse','extra_periodic_beyond_missing_channel_mean_mse')
    sums={key:sum(r['decomposition'][key] for r in rows) for key in keys}
    return dict(cases=len(rows),mean_mse={key:value/len(rows) for key,value in sums.items()},
        cell_template_explained_fraction=1-sums['within_cell_mse']/sums['total_mse'] if sums['total_mse'] else None,
        missing_mean_fraction=sums['missing_channel_mean_mse']/sums['total_mse'] if sums['total_mse'] else None,
        extra_subpixel_fraction=sums['extra_periodic_beyond_missing_channel_mean_mse']/sums['total_mse'] if sums['total_mse'] else None,
        bias_removed_algebraic_mse=sum(r['without_head_bias_mse'] for r in rows)/len(rows))


def run(parent,audit,output):
    root=Path(__file__).resolve().parents[2];parent,audit,output=(p.resolve() for p in (parent,audit,output))
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    current=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    old_parent=Path(current['freeze']['reference']['path']).resolve()
    if not old_parent.is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Reference outside neural build')
    old=json.loads((old_parent/'result.json').read_text(encoding='utf-8'))
    crossed=json.loads(audit.read_text(encoding='utf-8'))
    if digest(parent/'result.json')!=crossed['freeze']['parent']['result_sha256']:raise ValueError('Wrong phase audit')
    if digest(old_parent/'result.json')!=current['freeze']['reference']['result_sha256']:raise ValueError('Changed reference')
    cases=[c for c in current['freeze']['evaluation_cases'] if c['family']=='constant']
    if len(cases)!=4:raise ValueError('Need four frozen flat contents')
    models=[('canonical',parent,current),('native_phases',old_parent,old)]
    now=datetime.now(timezone.utc);campaign='neural-flat-subpixel-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={n:dict(path=str(p),result_sha256=digest(p/'result.json'),weights_sha256=d['weights_sha256']) for n,p,d in models},
        phase_audit=dict(path=str(audit),sha256=digest(audit)),flat_contents=cases,core_border=48,
        policy='Four existing flat RGB contents crossed with four phases, two fixed models; no fitting or correction selection.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('audit_flat_subpixels.py','joint_bayer_unet.py',
            'bayer_canonical.py','synthetic_bayer_data.py','run_synthetic_joint_pilot.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    rows=[];started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,status='incomplete',threads_intra=threads,
        versions=dict(torch=torch.__version__),
        limits=['Existing synthetic flat contents only, no natural-scene or noise qualification.',
            'Observed-site locking itself makes missing-channel bias spatially periodic; not all periodic energy implies independent subpixel errors.',
            'Subtracting the known output-head bias is algebra only; other features may compensate it, not a valid selected correction.',
            'No model modification, optimizer, new weights, images, default change or production integration.'])
    try:
        for name,folder,proof in models:
            config=proof['freeze']['config'];weights=folder/'synthetic-joint-weights.pt'
            if digest(weights)!=proof['weights_sha256']:raise ValueError('Weights changed')
            for source in ('joint_bayer_unet.py','synthetic_bayer_data.py'):
                if digest(Path(__file__).with_name(source))!=proof['freeze']['tools'][source]:raise ValueError('Model/data code changed')
            if config.get('canonical_input',False) and digest(Path(__file__).with_name('bayer_canonical.py'))!=proof['freeze']['tools']['bayer_canonical.py']:
                raise ValueError('Canonical transform changed')
            model=JointBayerUNet(config['width'])
            checkpoint=torch.load(weights,map_location='cpu',weights_only=True)
            if checkpoint['freeze_sha256']!=proof['freeze_sha256']:raise ValueError('Checkpoint provenance differs')
            model.load_state_dict(checkpoint['state_dict'],strict=True);model.requires_grad_(False);model.eval()
            with torch.inference_mode():
                for case in cases:
                    target=signal('constant',case['seed'],config['side'])[None]
                    if not torch.equal(target,target[:,:,:1,:1].expand_as(target)):raise ValueError('Expected perfectly flat RGB truth')
                    for phase,values in enumerate(PATTERNS):
                        pattern=torch.tensor([values]);packed=sample_rgb(target,pattern);sigma=torch.zeros_like(packed)
                        prediction=predict(model,packed/config['scale'],sigma,pattern,exact_observations=True,
                            canonical_input=config.get('canonical_input',False))*config['scale']
                        if not torch.equal(sample_rgb(prediction,pattern),packed):raise ValueError('Observed-site mismatch')
                        crop=(slice(None),slice(None),slice(48,-48),slice(48,-48));error=prediction-target
                        detail=decompose(error[crop],pattern)
                        if name=='canonical':
                            previous=[r for r in crossed['records'] if r['family']=='constant' and r['seed']==case['seed'] and r['phase_index']==phase]
                            if len(previous)!=1 or previous[0]['target_sha256']!=tensor_hash(target):raise ValueError('Changed flat audit identity')
                            old_sse=previous[0]['metrics']['output_sse'];new_sse=float(error[crop].square().sum())
                            if abs(new_sse-old_sse)>1e-6*max(1e-8,old_sse):raise ValueError('Flat replay differs')
                        contribution=head_bias_field(model,packed/config['scale'],sigma,pattern,config)
                        residual=error[crop]-contribution[crop]
                        rows.append(dict(model=name,seed=case['seed'],phase_index=phase,phase_name=PHASE_NAMES[phase],pattern=list(values),
                            target_rgb=target[0,:,0,0].tolist(),target_sha256=tensor_hash(target),output_sha256=tensor_hash(prediction),
                            decomposition=detail,head_bias_normalized=model.head.bias.tolist(),
                            head_bias_contribution_mse=float(contribution[crop].square().mean()),
                            without_head_bias_mse=float(residual.square().mean())))
            if digest(weights)!=proof['weights_sha256']:raise ValueError('Original weights changed')
        result['by_model']={name:summarize([r for r in rows if r['model']==name]) for name,_,_ in models}
        result['by_model_phase']={name:{phase:summarize([r for r in rows if r['model']==name and r['phase_name']==phase])
            for phase in PHASE_NAMES} for name,_,_ in models}
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Diagnostic code changed')
        result['status']='completed_flat_subpixel_audit_no_training'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['by_model'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','audit','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();run(a.parent,a.audit,a.output)
