"""Frozen-weight real CFA exploration with local sensor-derived deviations."""
from datetime import datetime,timezone
import argparse
import json
import os
from pathlib import Path
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run(plan_path=None,output=None):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    plan_path=plan_path or build/'local-sigma-plan-v001/plan.json'
    output=output or build/'local-sigma-inference-v001'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (plan_path,output)):raise ValueError('Dedicated inference paths required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if plan['status']!='sealed_local_sigma_photo_exploration':raise ValueError('Sealed plan required')
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan=plan,plan_sha256=sha256(plan_path),records=[],engineering=[],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('infer_sensor_sigma.py','photo_cfa_tiling.py','camera_noise_sigma.py')})
    def save():
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'result.json')
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92)
    try:
        with guard:
            import numpy as np
            import torch
            from edge_protected_noise import EdgeProtectedNoisyUNet
            from photo_cfa_tiling import correct_window,tiled_correct
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5)
            result['torch_version']=str(torch.__version__)
            old=json.loads((build/'real-photo-neural-v001/result.json').read_text(encoding='utf-8'))
            originals=json.loads((build/'real-photo-baselines-v001/plan.json').read_text(encoding='utf-8'))['models']
            for ref in plan['models']:
                if sha256(Path(ref['path']))!=ref['weights_sha256']:raise ValueError('Weights changed')
                state=torch.load(ref['path'],map_location='cpu',weights_only=True)['state_dict']
                model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False).to('cuda');model.load_state_dict(state,strict=True)
                for scene in plan['scenes']:
                    guard.check()
                    if sha256(Path(scene['input']))!=scene['input_sha256'] or sha256(Path(scene['sigma_path']))!=scene['sigma_sha256']:
                        raise ValueError('Input or sigma changed')
                    h,w,_=scene['sensor_shape'];source=np.memmap(scene['input'],dtype='<f4',mode='r',shape=(h,w))
                    sigma=np.load(scene['sigma_path'],mmap_mode='r',allow_pickle=False)
                    if not result['engineering']:
                        cpu=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False);cpu.load_state_dict(state,strict=True)
                        windows=np.array(source[:512,:512],copy=True)[None];maps=np.array(sigma[:512,:512],copy=True)[None]
                        with torch.inference_mode():
                            a=correct_window(cpu,windows,scene['pattern'],maps,'cpu')
                            b=correct_window(model,windows,scene['pattern'],maps,'cuda')
                            constant=correct_window(model,windows,scene['pattern'],.01,'cuda')
                            constant_map=correct_window(model,windows,scene['pattern'],np.full_like(windows,.01),'cuda')
                        error=float(np.max(np.abs(a-b)))
                        if error>2e-6 or not np.array_equal(constant,constant_map):raise ValueError('Map arithmetic bridge failed')
                        engineering=dict(cpu_gpu_map_max_error=error,constant_map_window_bit_exact=True)
                        original=next(r for r in originals if r['seed']==ref['seed'])
                        if original['weights_sha256']==ref['weights_sha256']:
                            target=output/'constant-bridge.f32';out=np.memmap(target,dtype='<f4',mode='w+',shape=(h,w))
                            tiled_correct(model,source,scene['pattern'],.01,out,'cuda',check=guard.check);out.flush();del out
                            expected=next(r for r in old['records'] if r['scene']==scene['name'] and r['seed']==ref['seed'] and r['sigma']==.01)
                            if sha256(target)!=expected['sha256']:raise ValueError('Whole constant output changed')
                            engineering.update(constant_whole_output_bit_exact=True,expected_sha256=expected['sha256'])
                        else:
                            engineering['historical_whole_bridge']='Not applicable: weights intentionally changed; original bridge preserved in prior proof.'
                        result['engineering'].append(engineering)
                        del cpu,a,b,constant,constant_map
                    target=output/f"{scene['name']}-seed{ref['seed']}-local.f32"
                    destination=np.memmap(target,dtype='<f4',mode='w+',shape=(h,w))
                    started=time.monotonic();torch.cuda.reset_peak_memory_stats()
                    geometry=tiled_correct(model,source,scene['pattern'],sigma,destination,'cuda',check=guard.check)
                    torch.cuda.synchronize();destination.flush();del destination
                    result['records'].append(dict(scene=scene['name'],seed=ref['seed'],path=str(target.resolve()),
                        sha256=sha256(target),geometry=geometry,elapsed_seconds=time.monotonic()-started,
                        peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated()))
                    save();print('LOCAL SIGMA',scene['name'],ref['seed'],flush=True)
                    del source,sigma
                del model,state;torch.cuda.empty_cache()
            result['status']='completed_local_sigma_inference_not_qualification'
            if any(sha256(Path(__file__).with_name(n))!=v for n,v in result['tools'].items()):raise ValueError('Code changed during inference')
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['memory']=guard.stats();save()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-local-sigma-inference-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--plan',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();run(args.plan,args.output)
