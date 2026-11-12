"""Explore the predeclared corrector on whole real CFA with qualified context."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import time

os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')

from memory_budget import MemoryGuard
from prepare_photo_comparison import digest


def run(root,plan_path,output):
    base=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (plan_path,output)):
        raise ValueError('Dedicated plan/output required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    cuda=json.loads((base/'cuda-probe-v002/result.json').read_text(encoding='utf-8'))
    if cuda['status']!='completed_cuda_resource_probe_not_training_qualification' or cuda.get('parity_passed') is not True:
        raise ValueError('Current CUDA arithmetic/resource probe required')
    output.mkdir(parents=True,exist_ok=False)
    report=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan_sha256=digest(plan_path),cuda_probe_sha256=digest(base/'cuda-probe-v002/result.json'),
        script_sha256=digest(Path(__file__)),records=[],engineering=[],
        limits=['Exploratory known real images, no clean reference or photographic qualification.',
            'Constant supplied balanced-camera sigma, not ISO calibration or an equivalent UI strength.',
            'CFA correction only; RCD and common display applied separately. Both seeds retained.'])
    def write():
        temp=output/'progress.tmp'
        temp.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        temp.replace(output/'result.json')
    write()
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92)
    try:
        with guard:
            import numpy as np
            import torch
            from edge_protected_noise import EdgeProtectedNoisyUNet
            from photo_cfa_tiling import correct_window,tiled_correct
            from audit_spectral_noise import source_contract
            from neural_inference_contract import INFERENCE_TOOLS
            frozen_source=source_contract(root,json.loads((base/'repeatability-control-protected-v001/result.json').read_text(encoding='utf-8')))
            if any(digest(Path(__file__).with_name(n))!=frozen_source['tools'][n] for n in INFERENCE_TOOLS):
                raise ValueError('Qualified inference arithmetic changed')
            report['inference_tools']={n:digest(Path(__file__).with_name(n)) for n in INFERENCE_TOOLS+('photo_cfa_tiling.py','infer_real_cfa.py')}
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            if not torch.cuda.is_available():raise ValueError('CUDA required; no hidden fallback')
            if torch.__version__!=cuda['torch_version']:raise ValueError('CUDA runtime changed')
            free,total=torch.cuda.mem_get_info()
            if free<4*1024**3:raise RuntimeError('GPU busy: less than4GiB available; defer, do not compete')
            torch.cuda.set_per_process_memory_fraction(.5)
            torch.backends.cuda.matmul.allow_tf32=False
            torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False
            torch.backends.cudnn.deterministic=True
            for ref in plan['models']:
                if digest(ref['path'])!=ref['weights_sha256']:raise ValueError('Predeclared model changed')
                state=torch.load(ref['path'],weights_only=True,map_location='cpu')['state_dict']
                model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False).to('cuda')
                model.load_state_dict(state,strict=True)
                for scene in plan['scenes']:
                    guard.check()
                    if digest(scene['input'])!=scene['input_sha256']:raise ValueError('Real source buffer changed')
                    h,w,_=scene['sensor_shape']
                    source=np.memmap(scene['input'],dtype='<f4',mode='r',shape=(h,w))
                    window=np.pad(source[:384,:384],((128,0),(128,0)),mode='reflect')[None]
                    if not report['engineering']:
                        cpu=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                        cpu.load_state_dict(state,strict=True)
                        with torch.inference_mode():
                            a=correct_window(cpu,window,scene['pattern'],.01,'cpu')
                            b=correct_window(model,window,scene['pattern'],.01,'cuda')
                            zero=correct_window(model,window,scene['pattern'],0.,'cuda')
                            batched=correct_window(model,np.repeat(window,4,axis=0),scene['pattern'],.01,'cuda')
                        error=float(np.max(np.abs(a-b)))
                        batch_error=float(np.max(np.abs(batched-b)))
                        if error>2e-6 or batch_error>2e-6 or not np.array_equal(zero,window):
                            raise ValueError('Real-window GPU/CPU, batch or zero-sigma contract failed')
                        report['engineering'].append(dict(source=scene['name'],cpu_gpu_max_error=error,
                            batch_max_error=batch_error,zero_sigma_bit_exact=True))
                        del cpu,a,b,zero,batched
                    for sigma in plan['sigma_scenarios']:
                        guard.check()
                        target=output/f"{scene['name']}-seed{ref['seed']}-sigma{sigma:.2f}.f32"
                        if target.exists():raise FileExistsError(target)
                        destination=np.memmap(target,dtype='<f4',mode='w+',shape=(h,w))
                        torch.cuda.reset_peak_memory_stats()
                        started=time.monotonic()
                        def progress(done,count):
                            if done%32==0 or done==count:
                                report['active']=dict(scene=scene['name'],seed=ref['seed'],sigma=sigma,tiles_done=done,tiles_total=count)
                                write()
                        geometry=tiled_correct(model,source,scene['pattern'],sigma,destination,'cuda',check=guard.check,progress=progress)
                        torch.cuda.synchronize()
                        destination.flush()
                        del destination
                        report['records'].append(dict(scene=scene['name'],seed=ref['seed'],sigma=sigma,
                            path=str(target.resolve()),sha256=digest(target),shape=[h,w,1],geometry=geometry,
                            elapsed_seconds=time.monotonic()-started,peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated()))
                        write();print('REAL CFA',scene['name'],ref['seed'],sigma,flush=True)
                    del source
                del model,state
                torch.cuda.empty_cache()
            report['status']='completed_real_cfa_inference_not_photographic_qualification'
            if any(digest(Path(__file__).with_name(n))!=h for n,h in report['inference_tools'].items()):
                raise ValueError('Inference code changed during computation')
    except Exception as error:
        report.update(status='stopped_on_error',error=str(error));raise
    finally:
        report['finished_at_utc']=datetime.now(timezone.utc).isoformat()
        report['memory']=guard.stats()
        write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-real-cfa-inference-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(report,stream,indent=2)
    print(report['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(Path(__file__).resolve().parents[2],args.plan,args.output)
