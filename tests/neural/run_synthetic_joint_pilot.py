"""Fixed joint-Bayer plumbing/learnability pilot on known procedural RGB only."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

import torch

from diagnose_train_learnability import digest,save
from joint_bayer_unet import JointBayerUNet,bilinear,sample_rgb
from synthetic_bayer_data import PATTERNS,FAMILIES,signal,noisy_observation


NOISE=((0.,0.),(.0002,.000025),(.0008,.0004))


def tensor_hash(array):
    return hashlib.sha256(array.detach().cpu().contiguous().numpy().tobytes()).hexdigest()


def metrics(target,baseline,output,border=48):
    if target.shape!=baseline.shape or target.shape!=output.shape or min(target.shape[-2:])<=2*border:
        raise ValueError('Matching images with sufficient core support required')
    if not all(torch.isfinite(v).all() for v in (target,baseline,output)):raise ValueError('Nonfinite result')
    target,baseline,output=(v[:,:,border:-border,border:-border] for v in (target,baseline,output))
    count=target.shape[0]*target.shape[2]*target.shape[3]
    before=baseline-target;after=output-target
    opponent=lambda error:torch.stack((error[:,0]-error[:,1],error[:,2]-error[:,1]),dim=1)
    gradient=lambda v:torch.stack(((v[:,1,1:-1,2:]-v[:,1,1:-1,:-2])*.5,
                                  (v[:,1,2:,1:-1]-v[:,1,:-2,1:-1])*.5),dim=1)
    ref_gradient=gradient(target);base_gradient=gradient(baseline);out_gradient=gradient(output)
    return dict(pixels=count,baseline_sse=float(before.square().sum()),output_sse=float(after.square().sum()),
        baseline_opponent_sse=float(opponent(before).square().sum()),output_opponent_sse=float(opponent(after).square().sum()),
        channel_bias_sum=after.sum(dim=(0,2,3)).tolist(),
        reference_gradient_energy=float(ref_gradient.square().sum()),
        baseline_gradient_projection=float((base_gradient*ref_gradient).sum()),
        output_gradient_projection=float((out_gradient*ref_gradient).sum()),
        output_gradient_sse=float((out_gradient-ref_gradient).square().sum()))


def aggregate(rows):
    if not rows:raise ValueError('Nonempty metric rows required')
    count=sum(r['metrics']['pixels'] for r in rows)
    sums={k:sum(r['metrics'][k] for r in rows) for k in ('baseline_sse','output_sse','baseline_opponent_sse',
        'output_opponent_sse','reference_gradient_energy','baseline_gradient_projection','output_gradient_projection')}
    ratio=lambda a,b:a/b if b>1e-14 else None
    baseline_mse=sums['baseline_sse']/(3*count)
    baseline_opponent_mse=sums['baseline_opponent_sse']/(2*count)
    return dict(cases=len(rows),pixels=count,baseline_mse=sums['baseline_sse']/(3*count),output_mse=sums['output_sse']/(3*count),
        mse_ratio=sums['output_sse']/sums['baseline_sse'] if baseline_mse>1e-12 else None,
        opponent_mse_ratio=sums['output_opponent_sse']/sums['baseline_opponent_sse'] if baseline_opponent_mse>1e-12 else None,
        relative_error_mse_floor=1e-12,
        baseline_gradient_gain=ratio(sums['baseline_gradient_projection'],sums['reference_gradient_energy']),
        output_gradient_gain=ratio(sums['output_gradient_projection'],sums['reference_gradient_energy']),
        channel_bias=[sum(r['metrics']['channel_bias_sum'][c] for r in rows)/count for c in range(3)])


def run(output):
    root=Path(__file__).resolve().parents[2];output=output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Use dedicated neural build')
    config=dict(seed=71,width=8,batch=4,steps=2048,learning_rate=.001,scale=2.,side=128,loss_border=48)
    cases=[dict(family=family,seed=7300+100*i+j,pattern=list(PATTERNS[j%4]))
        for i,family in enumerate(FAMILIES) for j in range(8)]
    tests=[dict(family=family,seed=8400+100*i+j,pattern=list(pattern),noise_index=k,
                noise_seed=95000+1000*i+100*j+k)
        for i,family in enumerate(FAMILIES) for j,pattern in enumerate(PATTERNS) for k in range(3)]
    now=datetime.now(timezone.utc);campaign='neural-synthetic-joint-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),config=config,
        train_cases=cases,evaluation_cases=tests,noise_coefficients_a_b=[list(v) for v in NOISE],
        domain='Signed balanced camera RGB/mosaic, unclipped; packing TL/TR/BL/BR, explicit Bayer tags.',
        conditioning='Sigma from noisy observation and known simulation coefficients, not clean-target sigma.',
        selection='One seed, Adam, fixed2048 steps, final checkpoint; no evaluation-dependent tuning.',
        relative_error_mse_floor=1e-12,
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('run_synthetic_joint_pilot.py','joint_bayer_unet.py','synthetic_bayer_data.py','camera_unet.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.manual_seed(config['seed'])
    torch.use_deterministic_algorithms(True)
    clean=torch.stack([signal(c['family'],c['seed'],config['side']) for c in cases])
    patterns=torch.tensor([c['pattern'] for c in cases],dtype=torch.int64)
    model=JointBayerUNet(config['width']);optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
    generator=torch.Generator().manual_seed(72);rows=[];losses=[];started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,status='incomplete',losses=losses,
        parameter_count=sum(p.numel() for p in model.parameters()),threads_intra=threads,
        versions=dict(torch=torch.__version__),
        limits=['Procedural train/evaluation families share construction rules; not natural-scene generalization.',
            'Signal-dependent Gaussian CFA noise is uncalibrated; no Canon noise-profile or ISO claim.',
            'Synthetic RGB is already camera RGB; no physical spectral response, optics, PRNU or real compression model.',
            'Evaluation against own bilinear baseline only, not RCD, Lightroom or another trained network.',
            'An ambiguous same-mosaic pair is a limitation witness, excluded from quality aggregate.',
            'No RAW training, camera-level validation, visual approval, default change or production hook.'])
    try:
        for step in range(1,config['steps']+1):
            ids=torch.randint(len(cases),(config['batch'],),generator=generator)
            target=clean[ids];pattern=patterns[ids];a,b=NOISE[(step-1)%len(NOISE)]
            noisy,sigma=noisy_observation(target,pattern,a,b,generator)
            model.train();optimizer.zero_grad(set_to_none=True)
            output_image=model(noisy/config['scale'],sigma/config['scale'],pattern)
            crop=(slice(None),slice(None),slice(48,-48),slice(48,-48))
            loss=(output_image[crop]-target[crop]/config['scale']).square().mean()
            loss.backward()
            if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):raise ValueError('Nonfinite gradient')
            optimizer.step();losses.append(float(loss.detach()))
            if step%256==0:print(f'TRAIN {step}/2048',flush=True)
        model.eval()
        with torch.inference_mode():
            for case in tests:
                target=signal(case['family'],case['seed'],config['side'])[None]
                pattern=torch.tensor([case['pattern']],dtype=torch.int64)
                a,b=NOISE[case['noise_index']]
                noisy,sigma=noisy_observation(target,pattern,a,b,torch.Generator().manual_seed(case['noise_seed']))
                before=tensor_hash(noisy);base=bilinear(noisy,pattern)
                prediction=model(noisy/config['scale'],sigma/config['scale'],pattern)*config['scale']
                if tensor_hash(noisy)!=before:raise ValueError('Input mosaic modified')
                rows.append(dict(**case,target_sha256=tensor_hash(target),noisy_sha256=before,sigma_sha256=tensor_hash(sigma),
                    output_sha256=tensor_hash(prediction),metrics=metrics(target,base,prediction),
                    output_min=float(prediction.min()),output_max=float(prediction.max())))
            a=torch.zeros(1,3,32,32);b=a.clone();b[:,0,1::2,1::2]=.5
            pattern=torch.tensor([PATTERNS[0]],dtype=torch.int64)
            first,second=sample_rgb(a,pattern),sample_rgb(b,pattern)
            if not torch.equal(first,second):raise ValueError('Ambiguity witness failed')
            result['ambiguity_witness']=dict(distinct_targets=True,identical_mosaics=True,
                first_target_sha256=tensor_hash(a),second_target_sha256=tensor_hash(b),mosaic_sha256=tensor_hash(first),
                interpretation='No deterministic single-image reconstruction can recover both targets from this same observation.')
        result['summary']=aggregate(rows)
        result['by_family']={family:aggregate([r for r in rows if r['family']==family]) for family in FAMILIES}
        result['by_noise']={str(i):aggregate([r for r in rows if r['noise_index']==i]) for i in range(3)}
        result['by_family_and_noise']={family:{str(i):aggregate([r for r in rows if r['family']==family and r['noise_index']==i])
            for i in range(3)} for family in FAMILIES}
        torch.save(dict(state_dict=model.state_dict(),config=config,freeze_sha256=digest(output/'freeze.json')),
            output/'synthetic-joint-weights.pt')
        result['weights_sha256']=digest(output/'synthetic-joint-weights.pt')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Study code changed')
        result['status']='completed_synthetic_joint_pilot_not_photographic_qualification'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args().output)
