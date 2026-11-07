"""Bounded from-scratch CFA training with full-update stop, atomic resume and fixed controls."""
import argparse
from datetime import datetime,timezone
import gc
import json
import os
from pathlib import Path
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from memory_trace import MemoryTrace
from dru_execution import load as load_execution,validate_sources


def write(path,value):
    temporary=path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8');temporary.replace(path)


def run(plan_path,output,seed,preflight=False,resume=False,execution_path=None):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (plan_path,output)):raise ValueError('Owned paths required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if seed not in plan['seeds'] or plan['status']!='sealed_dru_cfa_bootstrap':raise ValueError('Sealed declared training required')
    if (plan['data_arm'] not in ('reference','noise','details') or not isinstance(plan['data_seed'],int)
            or isinstance(plan['data_seed'],bool) or plan['data_seed']<0):raise ValueError('Explicit data arm and independent seed required')
    objective=plan.get('objective','mse')
    if objective not in ('mse','haar_noise_aware_v1'):raise ValueError('Unknown training objective')
    weight=plan.get('structure_weight',1.)
    if not isinstance(weight,(int,float)) or isinstance(weight,bool) or not 0<weight<=1:raise ValueError('Declared positive structure weight up to one required')
    execution=load_execution(execution_path,plan_path,root)
    validate_sources(plan,execution,root)
    if sha256(Path(plan['noise_source']))!=plan['noise_sha256']:raise ValueError('Noise source changed')
    output.mkdir(exist_ok=resume)
    previous=json.loads((output/'result.json').read_text(encoding='utf-8')) if resume else None
    result=dict(schema_version=1,status='started',seed=seed,preflight=preflight,plan_sha256=sha256(plan_path),
        started_at_utc=datetime.now(timezone.utc).isoformat(),losses=[],milestones=[],controls=[],data_seed=plan['data_seed'],data_arm=plan['data_arm'])
    result.update(objective=objective,structure_weight=weight,loss_components={})
    if execution is not None:result['execution']=dict(path=str(execution_path.resolve()),sha256=sha256(execution_path),contract=execution)
    if previous is not None:
        result['loss_components']=previous.get('loss_components',{})
        result['milestones']=previous['milestones'];result['controls']=previous['controls']
        result['attempt_history']=previous.get('attempt_history',[])+[dict(status=previous['status'],
            completed_volatile_steps=previous['completed_steps'],source_sha256=sha256(output/'result.json'),
            started_at_utc=previous['started_at_utc'],elapsed_seconds=previous['elapsed_seconds'])]
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    trace=MemoryTrace(output/f'memory-{stamp}.jsonl')
    resources=dict(hard_mib=6144,soft_mib=4608,system_fraction=.92) if execution is None else execution['resources']
    started=time.monotonic();guard=MemoryGuard(hard_bytes=resources['hard_mib']*1024**2,soft_bytes=resources['soft_mib']*1024**2,system_fraction=resources['system_fraction'],observer=trace.observe)
    completed=0;timings=[];deadline=datetime.fromisoformat(plan['training_deadline_utc'] if execution is None else execution['training_deadline_utc']).timestamp()
    controller=None
    if execution is not None and not preflight:
        from load_control import YieldController
        controller=YieldController(execution['load'])
    try:
        with trace,guard:
            trace.phase('torch-import')
            import torch
            from dru_cfa import create
            from dru_checkpoint import save,restore,fingerprint,link_identical_checkpoint
            from dru_training_data import example
            from dru_structure_objective import loss_components
            from dru_clean_variance import clean_sigma
            from dru_comparison_policy import scenario_for
            from dru_known_controls import evaluate
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('CUDA unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5);torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            def phase(name,step=None,micro=None):
                gpu=None
                if torch.cuda.is_initialized():
                    gpu=dict(allocated_bytes=torch.cuda.memory_allocated(),reserved_bytes=torch.cuda.memory_reserved(),
                        peak_allocated_bytes=torch.cuda.max_memory_allocated(),peak_reserved_bytes=torch.cuda.max_memory_reserved())
                trace.phase(name,completed if step is None else step,micro,gpu)
            phase('model-initialization')
            model,provenance=create(root,seed)
            result['initial_model_fingerprint']=fingerprint(model.state_dict())
            model=model.cuda();result['architecture']=provenance
            optimizer=torch.optim.Adam(model.parameters(),lr=plan['learning_rate'])
            context=dict(plan_sha256=sha256(plan_path),seed=seed,torch_version=str(torch.__version__))
            source=json.loads(Path(plan['noise_source']).read_text(encoding='utf-8'));scenarios=source['scenes']
            if resume:
                phase('checkpoint-restore')
                manifest=json.loads((output/'resume.json').read_text(encoding='utf-8'))
                if sha256(output/'resume.pt')!=manifest['sha256']:raise ValueError('Resume file changed')
                completed,result['losses']=restore(output/'resume.pt',model,optimizer,context)
                result['resumed_from_step']=completed
            elif plan.get('parent_checkpoints'):
                parent=next(r for r in plan['parent_checkpoints'] if r['seed']==seed)
                parent_path=Path(parent['path'])
                if not parent_path.resolve().is_relative_to(build.resolve()) or sha256(parent_path)!=parent['sha256']:raise ValueError('Continuation parent changed')
                parent_context=dict(context,plan_sha256=parent['scientific_plan_sha256'])
                completed,result['losses']=restore(parent_path,model,optimizer,parent_context)
                result.update(continued_from_step=completed,parent_checkpoint=parent)
            def report():
                result.update(completed_steps=completed,elapsed_seconds=time.monotonic()-started,step_seconds=timings)
                write(output/'progress.json',result)
            def check():
                guard.check()
                if time.time()>=deadline:raise TimeoutError('Training deadline reached')
                if (output.parent/'STOP').exists():raise InterruptedError('Requested full-update stop')
                if controller is not None and controller.check():raise InterruptedError('load_scheduler_yield')
            def snapshot(name):
                phase('checkpoint-save')
                path=output/name;save(path,model,optimizer,context,completed,result['losses'])
                row=dict(path=str(path.resolve()),sha256=sha256(path),step=completed)
                if name=='resume.pt':write(output/'resume.json',row)
                else:result['milestones'].append(row)
                return row
            def update(step):
                phase('update-boundary',step)
                check();optimizer.zero_grad(set_to_none=True);total=0.;parts=[0.,0.];tick=time.monotonic()
                for micro in range(4):
                    phase('data-generation',step,micro)
                    packed,sigma,tags,target=example((step-1)*4+micro,plan['data_seed'],scenarios,arm=plan['data_arm'])
                    phase('host-to-device',step,micro)
                    packed,sigma,tags,target=(x.cuda() for x in (packed,sigma,tags,target))
                    known=None
                    if objective=='haar_noise_aware_v1':
                        serial=(step-1)*4+micro;identity=280000000+plan['data_seed']*100000+serial
                        scenario=scenario_for(serial,identity,scenarios,plan['data_arm'])
                        known=clean_sigma(target,tags,scenario)
                    phase('forward',step,micro)
                    prediction=model.correct_observations(packed,sigma,tags)
                    combined,mse,structure=loss_components(prediction,target,sigma,objective,known_sigma=known,weight=weight)
                    loss=combined/4
                    if not torch.isfinite(loss):raise ValueError('Nonfinite training loss')
                    if loss.requires_grad:
                        phase('backward',step,micro);loss.backward()
                    total+=float(loss.detach())
                    parts[0]+=float(mse.detach())/4;parts[1]+=float(structure.detach())/4
                    del prediction,loss,combined,mse,structure,packed,sigma,tags,target,known
                phase('optimizer-update',step)
                torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True)
                optimizer.step();optimizer.zero_grad(set_to_none=True);torch.cuda.synchronize()
                result['loss_components'][str(step)]=dict(mse=parts[0],structure=parts[1])
                timings.append(time.monotonic()-tick);return total
            def control(step,limited=False):
                phase('known-control',step)
                optimizer.zero_grad(set_to_none=True);gc.collect();torch.cuda.empty_cache()
                field_index=0
                def control_check():
                    nonlocal field_index
                    phase('known-control',step,field_index);field_index+=1;check()
                value=evaluate(model,root,control_check,limit=2 if limited else None)
                path=output/f'control-{step:06d}.json';write(path,value)
                result['controls'].append(dict(step=step,path=str(path.resolve()),sha256=sha256(path),observations=value['observations']))
                gc.collect();torch.cuda.empty_cache();report()
            report();maximum=8 if preflight else plan['steps_per_seed']
            if completed==0 and not resume:
                snapshot('checkpoint-000000.pt')
                snapshot('resume.pt')
                control(0,limited=preflight)
            elif not preflight and completed in (0,*plan['milestones']) and not any(c['step']==completed for c in result['controls']):
                control(completed)
            for step in range(completed+1,maximum+1):
                result['losses'].append(update(step));completed=step
                if preflight and step==4:snapshot('bridge.pt')
                if step%32==0 or step==maximum:
                    snapshot('resume.pt');report();print('DRU CFA',seed,step,'/',maximum,flush=True)
                if not preflight and step in plan['milestones']:
                    snapshot(f'checkpoint-{step:06d}.pt');control(step)
            if preflight:
                reference=fingerprint(dict(model=model.state_dict(),optimizer=optimizer.state_dict()))
                expected=result['losses'][-4:].copy()
                # Release old Adam before loading its full CPU state, as in the qualified engineering probe.
                del model,optimizer;gc.collect();torch.cuda.empty_cache();guard.check()
                phase('checkpoint-restore')
                model,_=create(root,seed);model=model.cuda()
                optimizer=torch.optim.Adam(model.parameters(),lr=plan['learning_rate'])
                completed,result['losses']=restore(output/'bridge.pt',model,optimizer,context)
                gc.collect();torch.cuda.empty_cache();guard.check()
                for step in range(completed+1,9):result['losses'].append(update(step));completed=step
                result['resume_bit_exact']=fingerprint(dict(model=model.state_dict(),optimizer=optimizer.state_dict()))==reference and result['losses'][-4:]==expected
                if not result['resume_bit_exact']:raise ValueError('Actual data-loop resume changed trajectory')
                result['status']='passed_dru_training_preflight_discarded'
            else:
                snapshot('weights.pt');result['status']='completed_dru_bootstrap_not_photographic_qualification'
                final=output/f'checkpoint-{completed:06d}.pt'
                result['deduplicated_checkpoint_paths']=[]
                if final.exists():
                    for name in ('weights.pt','resume.pt'):
                        if link_identical_checkpoint(final,output/name):result['deduplicated_checkpoint_paths'].append(name)
            result['peak_cuda_allocated_bytes']=torch.cuda.max_memory_allocated()
            del model,optimizer;gc.collect();torch.cuda.empty_cache()
    except (TimeoutError,InterruptedError) as error:
        # Full-update boundary: no half-step is saved. A control can also observe this request.
        if 'optimizer' in locals():
            optimizer.zero_grad(set_to_none=True);snapshot('resume.pt')
        result.update(status='paused_dru_training',reason=str(error))
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(completed_steps=completed,elapsed_seconds=time.monotonic()-started,memory=guard.stats(),memory_trace=trace.stats())
        if controller is not None:result['last_load_sample']=controller.last
        write(output/'result.json',result);write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        path=root/'docs/validation/raw'/f'neural-dru-training-v001-{seed}-{stamp}.json'
        with path.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--seed',type=int,required=True)
    parser.add_argument('--preflight',action='store_true');parser.add_argument('--resume',action='store_true')
    parser.add_argument('--execution',type=Path)
    args=parser.parse_args();run(args.plan,args.output,args.seed,args.preflight,args.resume,args.execution)
