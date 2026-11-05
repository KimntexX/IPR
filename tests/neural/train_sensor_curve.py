"""Bounded CUDA continuation, immutable milestones and resumable technical state."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def cpu_tree(value):
    import torch
    if isinstance(value,torch.Tensor):return value.detach().cpu().clone()
    if isinstance(value,dict):return {k:cpu_tree(v) for k,v in value.items()}
    if isinstance(value,list):return [cpu_tree(v) for v in value]
    if isinstance(value,tuple):return tuple(cpu_tree(v) for v in value)
    return value


def snapshot(path,model,optimizer,context,step,losses,counts):
    import torch
    from training_resume import validate_state
    state=dict(schema_version=1,context=context,completed_step=step,losses=losses,exposure_counts=counts,
        model_state=cpu_tree(model.state_dict()),optimizer_state=cpu_tree(optimizer.state_dict()),torch_cpu_rng=torch.get_rng_state())
    validate_state(state,context)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,suffix='.tmp',delete=False) as stream:
            temporary=Path(stream.name);torch.save(state,stream);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return dict(path=str(path.resolve()),sha256=sha256(path),step=step)


def run(output,seed,preflight=False,resume=False):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):raise ValueError('Owned output required')
    output.mkdir(exist_ok=resume,parents=True)
    plan_path=build/'sensor-learning-plan-v001/plan.json';plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if plan['status']!='sealed_sensor_learning_continuation':raise ValueError('Sealed plan required')
    ref=next(r for r in plan['models'] if r['seed']==seed)
    if sha256(Path(ref['path']))!=ref['weights_sha256'] or sha256(Path(plan['dataset']['path']))!=plan['dataset']['sha256']:
        raise ValueError('Initializer or dataset changed')
    result=dict(schema_version=1,status='started',seed=seed,preflight=preflight,plan_sha256=sha256(plan_path),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        config=plan['config'],milestones=[],step_seconds=[],script_sha256=sha256(Path(__file__)),
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('sensor_curve_noise.py','train_sensor_curve.py','noisy_training_policy.py')})
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92)
    started=time.monotonic();losses=[];counts={};completed=0
    def report():
        result.update(completed_steps=completed,elapsed_seconds=time.monotonic()-started)
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'result.json')
    try:
        with guard:
            import numpy as np
            import torch
            from bayer_canonical import predict,canonicalize
            from noisy_moment_unet import NoisyMomentUNet
            from noisy_training_policy import configure_trainable
            from sensor_curve_noise import observation
            from synthetic_bayer_data import PATTERNS
            from joint_bayer_unet import sample_rgb,bilinear
            from reconstruction_loss import energy_balanced_loss
            from clean_cfa_loss import clean_cfa_loss
            from cfa_structure_loss import structure_loss
            from run_synthetic_joint_pilot import tensor_hash
            from training_resume import restore_checkpoint
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.manual_seed(seed)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5);torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            result['torch_version']=str(torch.__version__)
            config=dict(plan['config'],steps=8 if preflight else plan['config']['steps'])
            result['config']=config
            context=dict(config=config,seed=seed,plan_sha256=sha256(plan_path),tools=result['tools'])
            model=NoisyMomentUNet(8).to('cuda');model.load_state_dict(torch.load(ref['path'],weights_only=True,map_location='cpu')['state_dict'],strict=True)
            result['training_mask']=configure_trainable(model,'cfa_front',True)
            optimizer=torch.optim.Adam(model.parameters(),lr=config['learning_rate'])
            if resume:
                completed,losses,counts,meta=restore_checkpoint(output/'resume.pt',model,optimizer,context)
                result['resume']=meta
            initial_frozen={n:p.detach().cpu().clone() for n,p in model.named_parameters() if not p.requires_grad}
            result['initial_checkpoint']=snapshot(output/'resume.pt',model,optimizer,context,completed,losses,counts)
            if not resume:
                result['milestones'].append(snapshot(output/'checkpoint-000000.pt',model,optimizer,context,0,losses,counts))
            dataset=Path(plan['dataset']['path']);timings=[]
            def update(step,stream):
                guard.check();torch.cuda.synchronize();tick=time.monotonic();optimizer.zero_grad(set_to_none=True);total=0.
                for position in range(4):
                    serial=4*(step-1)+position;entry=plan['entries'][serial%len(plan['entries'])]
                    stream.seek(entry['serial']*3*256*256*4);data=stream.read(3*256*256*4)
                    if len(data)!=3*256*256*4:raise ValueError('Truncated training target')
                    truth=torch.from_numpy(np.frombuffer(data,dtype='<f4').copy().reshape(1,3,256,256))
                    if tensor_hash(truth)!=entry['target_sha256']:raise ValueError('Target changed')
                    tags=torch.tensor([PATTERNS[entry['phase']]])
                    level=serial%4;scenario=None if level==0 else plan['scenarios'][level-1]
                    values,sigma=observation(truth,tags,scenario,config['training_seed_base']+serial)
                    values,sigma,truth,tags=values.to('cuda')/2,sigma.to('cuda')/2,truth.to('cuda')/2,tags.to('cuda')
                    image=predict(model,values,sigma,tags,False,True);baseline=bilinear(values,tags)
                    loss=energy_balanced_loss(image,truth,baseline,2.,112,'tempered')/4
                    if level:
                        clean,deviations,labels,_=canonicalize(sample_rgb(truth,tags),sigma,tags)
                        anchored=model.correct_observations(clean,deviations,labels)
                        noisy,_,_,_=canonicalize(values,sigma,tags);corrected=model.correct_observations(noisy,deviations,labels)
                        loss=loss+(clean_cfa_loss(anchored,clean,baseline,truth,2.,112)+structure_loss(corrected,clean,deviations,56,0.))/4
                    if not torch.isfinite(loss):raise ValueError('Nonfinite loss')
                    if loss.requires_grad:loss.backward()
                    elif level:raise ValueError('Missing correction gradient')
                    total+=float(loss.detach())
                    key=f"{entry['family']}/phase{entry['phase']}/scenario{level}";counts[key]=counts.get(key,0)+1
                torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True);optimizer.step();torch.cuda.synchronize()
                return total,time.monotonic()-tick
            with dataset.open('rb') as stream:
                preflight_state=None;reference_state=None
                for step in range(completed+1,config['steps']+1):
                    if time.monotonic()-started>2700:raise TimeoutError('Bounded per-seed45minute budget reached')
                    if step%64==1 and torch.cuda.mem_get_info()[0]<2*1024**3:raise RuntimeError('GPU became busy; retain checkpoint and stop')
                    loss,elapsed=update(step,stream);losses.append(loss);completed=step;timings.append(elapsed)
                    if preflight and step==4:
                        preflight_state=snapshot(output/'resume-bridge.pt',model,optimizer,context,step,losses.copy(),counts.copy())
                    if step%64==0 or step==config['steps']:
                        result['checkpoint']=snapshot(output/'resume.pt',model,optimizer,context,step,losses,counts)
                        if step in config['checkpoints'] and not preflight:
                            result['milestones'].append(snapshot(output/f'checkpoint-{step:06d}.pt',model,optimizer,context,step,losses,counts))
                        result['last_loss']=loss;result['step_seconds']=timings;report();print('STEP',seed,step,'/',config['steps'],flush=True)
                if preflight:
                    reference_state=cpu_tree(model.state_dict())
                    restored,restored_losses,restored_counts,_=restore_checkpoint(output/'resume-bridge.pt',model,optimizer,context)
                    counts.clear();counts.update(restored_counts)
                    for step in range(restored+1,9):update(step,stream)
                    result['resume_bit_exact']=all(torch.equal(value,model.state_dict()[name].detach().cpu()) for name,value in reference_state.items())
                    if not result['resume_bit_exact']:raise ValueError('CUDA checkpoint resume changed trajectory')
            if not all(torch.equal(value,dict(model.named_parameters())[name].detach().cpu()) for name,value in initial_frozen.items()):
                raise ValueError('Frozen reconstruction changed')
            result.update(status='passed_sensor_curve_preflight_discarded' if preflight else 'completed_sensor_curve_continuation_not_quality_qualification',
                frozen_reconstruction_bit_exact=True,losses=losses,exposure_counts=counts,
                median_seconds_per_update=statistics.median(timings[2:]),peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated())
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['memory']=guard.stats();report()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-sensor-curve-v001-{seed}-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--seed',type=int,choices=(71,73),required=True);parser.add_argument('--preflight',action='store_true');parser.add_argument('--resume',action='store_true')
    args=parser.parse_args();run(args.output,args.seed,args.preflight,args.resume)
