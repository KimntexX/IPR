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
from joint_bayer_unet import JointBayerUNet,bilinear,sample_rgb,project_exact_observations
from synthetic_bayer_data import PATTERNS,FAMILIES,signal,noisy_observation
from reconstruction_gates import gate_config,qualifies
from bayer_canonical import predict


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


def run(output,stage='joint',reference=None):
    if stage not in ('joint','reconstruction','reconstruction-consistent','canonical-reconstruction'):raise ValueError('Unknown training stage')
    root=Path(__file__).resolve().parents[2];output=output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Use dedicated neural build')
    config=dict(seed=71,width=8,batch=4,steps=2048,learning_rate=.001,scale=2.,side=128,loss_border=48)
    config['exact_observations']=stage in ('reconstruction-consistent','canonical-reconstruction')
    config['canonical_input']=stage=='canonical-reconstruction'
    reference_proof=None
    if reference is not None and not config['exact_observations']:raise ValueError('Reference only used for consistency ablation')
    if config['exact_observations']:
        if reference is None:raise ValueError('Consistent reconstruction requires frozen unconstrained reference')
        reference=reference.resolve()
        if not reference.is_relative_to((root/'build-patch_neural_restoration').resolve()):raise ValueError('Reference outside neural build')
        reference_proof=json.loads((reference/'result.json').read_text(encoding='utf-8'))
        if reference_proof['status']!='completed_synthetic_reconstruction_stage_not_photographic_qualification':
            raise ValueError('Reference reconstruction is incomplete')
        expected_stage='reconstruction-consistent' if config['canonical_input'] else 'reconstruction'
        if reference_proof['freeze']['training_stage']!=expected_stage:raise ValueError('Unexpected reconstruction reference')
        if digest(reference/'synthetic-joint-weights.pt')!=reference_proof['weights_sha256']:raise ValueError('Reference weights changed')
    cases=[dict(family=family,seed=7300+100*i+j,pattern=list(PATTERNS[j%4]))
        for i,family in enumerate(FAMILIES) for j in range(8)]
    tests=[dict(family=family,seed=8400+100*i+j,pattern=list(pattern),noise_index=k,
                noise_seed=95000+1000*i+100*j+k)
        for i,family in enumerate(FAMILIES) for j,pattern in enumerate(PATTERNS)
        for k in (range(3) if stage=='joint' else (0,))]
    prefix={'joint':'neural-synthetic-joint-v001-','reconstruction':'neural-synthetic-reconstruction-v001-',
        'reconstruction-consistent':'neural-synthetic-consistent-reconstruction-v001-',
        'canonical-reconstruction':'neural-synthetic-canonical-reconstruction-v001-'}[stage]
    now=datetime.now(timezone.utc);campaign=prefix+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),config=config,
        train_cases=cases,evaluation_cases=tests,noise_coefficients_a_b=[list(v) for v in NOISE],
        domain='Signed balanced camera RGB/mosaic, unclipped; packing TL/TR/BL/BR, explicit Bayer tags.',
        conditioning='Sigma from noisy observation and known simulation coefficients, not clean-target sigma.',
        selection='One seed, Adam, fixed2048 steps, final checkpoint; no evaluation-dependent tuning.',
        relative_error_mse_floor=1e-12,
        training_stage=stage,training_noise_indices=[0,1,2] if stage=='joint' else [0],
        gates=gate_config() if stage!='joint' else None,
        reference=None if reference is None else dict(path=str(reference),result_sha256=digest(reference/'result.json'),
            weights_sha256=digest(reference/'synthetic-joint-weights.pt')),
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('run_synthetic_joint_pilot.py','joint_bayer_unet.py','synthetic_bayer_data.py','camera_unet.py','reconstruction_gates.py','bayer_canonical.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.manual_seed(config['seed'])
    torch.use_deterministic_algorithms(True)
    clean=torch.stack([signal(c['family'],c['seed'],config['side']) for c in cases])
    patterns=torch.tensor([c['pattern'] for c in cases],dtype=torch.int64)
    model=JointBayerUNet(config['width']);optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
    reference_model=None
    if reference_proof is not None:
        # Model construction consumes RNG, but training uses a dedicated generator
        # and these blocks contain no stochastic layers.
        reference_model=JointBayerUNet(config['width'])
        checkpoint=torch.load(reference/'synthetic-joint-weights.pt',map_location='cpu',weights_only=True)
        if checkpoint['freeze_sha256']!=reference_proof['freeze_sha256']:raise ValueError('Reference checkpoint mismatch')
        reference_model.load_state_dict(checkpoint['state_dict'],strict=True);reference_model.requires_grad_(False);reference_model.eval()
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
    if stage!='joint':
        result['limits'].append('No added noise and sigma0 throughout this stage; no denoising-quality conclusion.')
    try:
        for step in range(1,config['steps']+1):
            ids=torch.randint(len(cases),(config['batch'],),generator=generator)
            target=clean[ids];pattern=patterns[ids]
            a,b=NOISE[(step-1)%len(NOISE)] if stage=='joint' else NOISE[0]
            noisy,sigma=noisy_observation(target,pattern,a,b,generator)
            model.train();optimizer.zero_grad(set_to_none=True)
            output_image=predict(model,noisy/config['scale'],sigma/config['scale'],pattern,
                exact_observations=config['exact_observations'],canonical_input=config['canonical_input'])
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
                prediction=predict(model,noisy/config['scale'],sigma/config['scale'],pattern,
                    exact_observations=config['exact_observations'],canonical_input=config['canonical_input'])*config['scale']
                if config['exact_observations'] and not torch.equal(sample_rgb(prediction,pattern),noisy):
                    raise ValueError('Exact observed values were not preserved')
                if tensor_hash(noisy)!=before:raise ValueError('Input mosaic modified')
                core=(slice(None),slice(None),slice(48,-48),slice(48,-48))
                observed_error=sample_rgb(prediction[core],pattern)-sample_rgb(target[core],pattern)
                rows.append(dict(**case,target_sha256=tensor_hash(target),noisy_sha256=before,sigma_sha256=tensor_hash(sigma),
                    output_sha256=tensor_hash(prediction),metrics=metrics(target,base,prediction),
                    observed_site_mse=float(observed_error.square().mean()),
                    output_min=float(prediction.min()),output_max=float(prediction.max())))
                if reference_model is not None:
                    previous_rows=[r for r in reference_proof['records'] if r['family']==case['family'] and r['pattern']==case['pattern']]
                    if len(previous_rows)!=1:raise ValueError('Ambiguous reference case')
                    previous=previous_rows[0]
                    if previous['target_sha256']!=tensor_hash(target) or previous['noisy_sha256']!=before:
                        raise ValueError('Changed reference inputs')
                    replay=predict(reference_model,noisy/config['scale'],sigma/config['scale'],pattern,
                        exact_observations=reference_proof['freeze']['config'].get('exact_observations',False),
                        canonical_input=reference_proof['freeze']['config'].get('canonical_input',False))*config['scale']
                    replay_metrics=metrics(target,base,replay)
                    if abs(replay_metrics['output_sse']-previous['metrics']['output_sse'])>1e-6*max(1e-8,previous['metrics']['output_sse']):
                        raise ValueError('Reference model replay differs')
                    projected=replay if config['canonical_input'] else project_exact_observations(replay,noisy,sigma,pattern)
                    reference_key='reference_records' if config['canonical_input'] else 'projection_only_records'
                    result.setdefault(reference_key,[]).append(dict(**case,metrics=metrics(target,base,projected),
                        observed_site_mse=float((sample_rgb(projected[core],pattern)-sample_rgb(target[core],pattern)).square().mean())))
            a=torch.zeros(1,3,32,32);b=a.clone();b[:,0,1::2,1::2]=.5
            pattern=torch.tensor([PATTERNS[0]],dtype=torch.int64)
            first,second=sample_rgb(a,pattern),sample_rgb(b,pattern)
            if not torch.equal(first,second):raise ValueError('Ambiguity witness failed')
            result['ambiguity_witness']=dict(distinct_targets=True,identical_mosaics=True,
                first_target_sha256=tensor_hash(a),second_target_sha256=tensor_hash(b),mosaic_sha256=tensor_hash(first),
                interpretation='No deterministic single-image reconstruction can recover both targets from this same observation.')
        result['summary']=aggregate(rows)
        result['by_family']={family:aggregate([r for r in rows if r['family']==family]) for family in FAMILIES}
        noise_indices=sorted({r['noise_index'] for r in rows})
        result['by_noise']={str(i):aggregate([r for r in rows if r['noise_index']==i]) for i in noise_indices}
        result['by_family_and_noise']={family:{str(i):aggregate([r for r in rows if r['family']==family and r['noise_index']==i])
            for i in noise_indices} for family in FAMILIES}
        if stage!='joint':result['qualification']=qualifies(result,freeze['gates'])
        if reference_model is not None:
            reference_key='reference' if config['canonical_input'] else 'projection_only'
            result[reference_key+'_summary']=aggregate(result[reference_key+'_records'])
            result[reference_key+'_by_family']={family:aggregate([r for r in result[reference_key+'_records'] if r['family']==family]) for family in FAMILIES}
            if digest(reference/'synthetic-joint-weights.pt')!=freeze['reference']['weights_sha256']:raise ValueError('Reference weights changed')
        torch.save(dict(state_dict=model.state_dict(),config=config,freeze_sha256=digest(output/'freeze.json')),
            output/'synthetic-joint-weights.pt')
        result['weights_sha256']=digest(output/'synthetic-joint-weights.pt')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Study code changed')
        result['status']=('completed_synthetic_joint_pilot_not_photographic_qualification' if stage=='joint'
            else 'completed_synthetic_reconstruction_stage_not_photographic_qualification')
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--stage',choices=('joint','reconstruction','reconstruction-consistent','canonical-reconstruction'),default='joint')
    p.add_argument('--reference',type=Path)
    a=p.parse_args();run(a.output,a.stage,a.reference)
