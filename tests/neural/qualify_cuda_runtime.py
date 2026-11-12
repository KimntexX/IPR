"""Bounded CUDA arithmetic/training resource probe; weights are discarded."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import statistics
import time

os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')

from memory_budget import MemoryGuard


def run(root,output,reference=None):
    base=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()) or (reference and not reference.resolve().is_relative_to(base.resolve())):
        raise ValueError('Dedicated neural paths required')
    output.mkdir(parents=True,exist_ok=False)
    # CUDA runtime has a separate committed-memory budget; global physical92% stays.
    guard=MemoryGuard(hard_bytes=(6 if reference else 2)*1024**3,
        soft_bytes=(4608 if reference else 1280)*1024**2,system_fraction=.92)
    result=dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),status='incomplete',
        scope='Resource/arithmetic engineering only; no model selection or photographic training.',records=[])
    try:
        with guard:
            import torch
            from bayer_canonical import predict,canonicalize
            from campaign_artifacts import campaign_models
            from clean_cfa_loss import clean_cfa_loss
            from cfa_structure_loss import structure_loss
            from context_data import extend_texture
            from diagnose_train_learnability import digest,save
            from joint_bayer_unet import sample_rgb,bilinear
            from noisy_moment_unet import NoisyMomentUNet,FORWARD_TOOLS
            from edge_protected_noise import EdgeProtectedNoisyUNet
            from noisy_training_policy import configure_trainable
            from reconstruction_loss import energy_balanced_loss
            from run_noisy_moment_pilot import observation
            from synthetic_bayer_data import PATTERNS
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            result['torch_version']=str(torch.__version__)
            result['script_sha256']=digest(Path(__file__))
            if reference is None:
                folder=campaign_models(base/'repeatability-control-campaign-v001')[0]
                proof=json.loads((folder/'result.json').read_text(encoding='utf-8'))
                weights=folder/'weights.pt'
                if digest(weights)!=proof['weights_sha256'] or proof['seed']!=71:
                    raise ValueError('Predeclared seed71 weights changed')
                model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                model.load_state_dict(torch.load(weights,weights_only=True)['state_dict'],strict=True)
                truth=extend_texture('chromatic_wave',16000000,256)
                packets=[]
                with torch.inference_mode():
                    for phase,pattern in enumerate(PATTERNS):
                        tags=torch.tensor([pattern])
                        for level in (0,1,2):
                            guard.check()
                            values,sigma=observation(truth,tags,level,350000000+phase*3+level)
                            values,sigma=values/2,sigma/2
                            expected=predict(model,values,sigma,tags,False,True)
                            packets.append(dict(phase=phase,level=level,values=values,sigma=sigma,tags=tags,truth=truth/2,expected=expected))
                torch.save(packets,output/'reference.pt')
                result.update(status='prepared_pinned_cpu_cuda_reference',weights_path=str(weights.resolve()),
                    weights_sha256=digest(weights),reference_sha256=digest(output/'reference.pt'),
                    tools={n:digest(Path(__file__).with_name(n)) for n in FORWARD_TOOLS},
                    max_absolute_tolerance=2e-6,cases=len(packets))
            else:
                frozen=json.loads((reference/'result.json').read_text(encoding='utf-8'))
                if (frozen['status']!='prepared_pinned_cpu_cuda_reference'
                        or frozen['reference_sha256']!=digest(reference/'reference.pt')
                        or frozen['weights_sha256']!=digest(Path(frozen['weights_path']))
                        or frozen['script_sha256']!=digest(Path(__file__))
                        or any(digest(Path(__file__).with_name(n))!=h for n,h in frozen['tools'].items())):
                    raise ValueError('Pinned reference contract changed')
                if not torch.cuda.is_available():
                    raise ValueError('CUDA unavailable; no CPU fallback')
                torch.backends.cuda.matmul.allow_tf32=False
                torch.backends.cudnn.allow_tf32=False
                torch.backends.cudnn.benchmark=False
                torch.backends.cudnn.deterministic=True
                torch.cuda.set_per_process_memory_fraction(.5,0)
                torch.cuda.reset_peak_memory_stats()
                model=EdgeProtectedNoisyUNet(8).eval()
                model.load_state_dict(torch.load(frozen['weights_path'],weights_only=True,map_location='cpu')['state_dict'],strict=True)
                model=model.to('cuda')
                packets=torch.load(reference/'reference.pt',weights_only=True,map_location='cpu')
                result.update(reference_sha256=digest(reference/'result.json'),gpu=torch.cuda.get_device_name(0),
                    cuda_version=torch.version.cuda,tf32=False,precision='float32',deterministic=True,
                    max_absolute_tolerance=frozen['max_absolute_tolerance'])
                with torch.inference_mode():
                    for packet in packets:
                        guard.check()
                        values,sigma,tags=(packet[n].to('cuda') for n in ('values','sigma','tags'))
                        a=predict(model,values,sigma,tags,False,True)
                        b=predict(model,values,sigma,tags,False,True)
                        error=(a.cpu()-packet['expected']).double()
                        result['records'].append(dict(phase=packet['phase'],level=packet['level'],
                            max_absolute_error=float(error.abs().max()),mean_absolute_error=float(error.abs().mean()),
                            repeat_bit_exact=bool(torch.equal(a,b))))
                del model
                torch.cuda.empty_cache()
                model=NoisyMomentUNet(8).to('cuda')
                model.load_state_dict(torch.load(frozen['weights_path'],weights_only=True,map_location='cpu')['state_dict'],strict=True)
                configure_trainable(model,'cfa_front',True)
                optimizer=torch.optim.Adam([p for p in model.parameters() if p.requires_grad],lr=.00025)
                training=[p for p in packets if p['level']>0][:4]
                times=[]
                for step in range(12):
                    guard.check()
                    torch.cuda.synchronize()
                    started=time.perf_counter()
                    optimizer.zero_grad(set_to_none=True)
                    for packet in training:
                        values,sigma,tags,truth=(packet[n].to('cuda') for n in ('values','sigma','tags','truth'))
                        image=predict(model,values,sigma,tags,False,True)
                        baseline=bilinear(values,tags)
                        clean,deviations,labels,_=canonicalize(sample_rgb(truth,tags),sigma,tags)
                        anchored=model.correct_observations(clean,deviations,labels)
                        canonical,_,_,_=canonicalize(values,sigma,tags)
                        corrected=model.correct_observations(canonical,deviations,labels)
                        loss=(energy_balanced_loss(image,truth,baseline,2.,112,'tempered')
                            +clean_cfa_loss(anchored,clean,baseline,truth,2.,112)
                            +structure_loss(corrected,clean,deviations,56,0.))/4
                        if not torch.isfinite(loss):
                            raise ValueError('Nonfinite CUDA training loss')
                        loss.backward()
                    torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True)
                    optimizer.step()
                    torch.cuda.synchronize()
                    elapsed=time.perf_counter()-started
                    if step>=2:times.append(elapsed)
                result.update(training_steps=12,discarded_warmup_steps=2,timed_step_seconds=times,
                    median_seconds_per_update=statistics.median(times),
                    peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated(),peak_cuda_reserved_bytes=torch.cuda.max_memory_reserved(),
                    training_limit='Unprotected training graph, protected parity graph. Precomputed inputs, H2D transfers included, no procedural generation/validation/checkpoint IO. Projection to long runs is not measured wall-clock duration.',
                    parity_passed=all(r['repeat_bit_exact'] and r['max_absolute_error']<=frozen['max_absolute_tolerance'] for r in result['records']),
                    status='completed_cuda_resource_probe_not_training_qualification')
                del optimizer,model
                torch.cuda.empty_cache()
    except Exception as error:
        result.update(status='failed_cuda_engineering_probe',error=str(error))
        raise
    finally:
        result['memory']=guard.stats()
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        if reference:
            stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
            path=root/'docs/validation/raw'/('neural-cuda-engineering-v001-'+stamp+'.json')
            with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
    print(result['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reference',type=Path)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output,args.reference)
