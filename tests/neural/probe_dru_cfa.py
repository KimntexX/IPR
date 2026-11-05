"""Discarded resource/arithmetic probe, no useful weights or quality verdict."""
from datetime import datetime,timezone
import argparse
import json
import os
from pathlib import Path
import statistics
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run(output):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):raise ValueError('Owned output required')
    output.mkdir(exist_ok=False)
    source=build/'local-sigma-plan-v001/plan.json';source_plan=json.loads(source.read_text(encoding='utf-8'))
    protocol=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),source_sha256=sha256(source),
        seed=9101,steps=6,accumulation=4,packed_context=512,packed_core=128,learning_rate=.0001,
        dtype='float32',input_channels=8,output_channels=4,pretrained_weights=False,
        policy='Official topology from pinned source, RANDOM weights, direct camera-CFA residual; no frozenRGB reconstruction in loss. One synthetic engineering fixture, not a learning experiment. All updated weights discarded.',
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('dru_cfa.py','probe_dru_cfa.py','sensor_curve_noise.py')})
    with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(protocol,stream,indent=2)
    result=dict(schema_version=1,status='started',plan=protocol,updates=[])
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92);started=time.monotonic()
    try:
        with guard:
            import torch
            from dru_cfa import create
            from sensor_curve_noise import observation
            from joint_bayer_unet import sample_rgb
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5);torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            model,provenance=create(root,protocol['seed']);model=model.to('cuda');result['architecture']=provenance
            result['torch_version']=str(torch.__version__)
            y,x=torch.meshgrid(torch.linspace(0,1,1024),torch.linspace(0,1,1024),indexing='ij')
            truth=torch.stack((.15+.3*x,.18+.25*y,.22+.1*(x+y)))[None]
            truth[:,:,448:480,448:576]=-.02;truth[:,:,544:576,448:576]=1.2
            tags=torch.tensor([[0,1,3,2]])
            target=sample_rgb(truth,tags).to('cuda')/2;device_tags=tags.to('cuda')
            values,sigma=observation(truth,tags,source_plan['scenes'][0],91000000)
            values,sigma=values.to('cuda')/2,sigma.to('cuda')/2
            with torch.inference_mode():
                initial=model.correct_observations(values,sigma,device_tags)
                result['initial_positive_sigma_identity_bit_exact']=bool(torch.equal(initial,values))
                del initial
            torch.cuda.empty_cache()
            if not result['initial_positive_sigma_identity_bit_exact']:raise ValueError('Zero head changed CFA')
            optimizer=torch.optim.Adam(model.parameters(),lr=protocol['learning_rate'])
            for step in range(6):
                guard.check()
                if time.monotonic()-started>180:raise TimeoutError('Three-minute engineering budget reached')
                optimizer.zero_grad(set_to_none=True);total=0.;torch.cuda.synchronize();tick=time.monotonic()
                for micro in range(4):
                    scenario=None if micro==0 else source_plan['scenes'][micro-1]
                    observed,deviation=observation(truth,tags,scenario,91000100+4*step+micro)
                    values,sigma=observed.to('cuda')/2,deviation.to('cuda')/2
                    prediction=model.correct_observations(values,sigma,device_tags)
                    loss=(prediction[:,:,192:320,192:320]-target[:,:,192:320,192:320]).square().mean()/4
                    if not torch.isfinite(loss):raise ValueError('Nonfinite engineering loss')
                    if loss.requires_grad:loss.backward()
                    total+=float(loss.detach())
                    del prediction,loss,observed,deviation
                torch.nn.utils.clip_grad_norm_(model.parameters(),1.,error_if_nonfinite=True);optimizer.step();torch.cuda.synchronize()
                result['updates'].append(dict(step=step+1,seconds=time.monotonic()-tick,fixture_loss=total))
                print('DRU CFA PREFLIGHT',step+1,'/6',flush=True)
            result['non_tail_gradient_present']=any(p.grad is not None and bool((p.grad!=0).any()) for n,p in model.named_parameters() if 'm_tail' not in n)
            small=values[:,:,:64,:64].contiguous();small_sigma=sigma[:,:,:64,:64].contiguous()
            cpu,_=create(root,protocol['seed'])
            cpu.load_state_dict({n:p.detach().cpu() for n,p in model.state_dict().items()},strict=True)
            with torch.inference_mode():
                reference=cpu.correct_observations(small.cpu(),small_sigma.cpu(),tags)
                actual=model.correct_observations(small,small_sigma,device_tags)
                repeat=model.correct_observations(small,small_sigma,device_tags)
                result['cpu_gpu_nonzero_tail_max_error']=float((reference-actual.cpu()).abs().max())
                result['gpu_repeat_bit_exact']=bool(torch.equal(actual,repeat))
                result['zero_sigma_identity']=bool(torch.equal(model.correct_observations(small,torch.zeros_like(small),device_tags),small))
            if result['cpu_gpu_nonzero_tail_max_error']>2e-6 or not result['gpu_repeat_bit_exact'] or not result['zero_sigma_identity'] or not result['non_tail_gradient_present']:
                raise ValueError('New architecture arithmetic/gradient contract failed')
            result.update(status='completed_discarded_dru_cfa_engineering_not_quality_qualification',
                median_seconds_per_update=statistics.median(r['seconds'] for r in result['updates'][2:]),
                peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated(),weights_retained=False)
            del model,cpu,optimizer;torch.cuda.empty_cache()
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        with (output/'result.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-cfa-engineering-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);run(parser.parse_args().output)
