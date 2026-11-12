"""Frozen small multi-scene SYNTHETIC pipeline test; no photo qualification.

8 train / 4 validation / 4 test scene seeds, architecture and 64 steps fixed
before any metric. No checkpoint selection, hyperparameter search or weights
saved. Gaussian variance proxy uses clean synthetic signal: oracle conditioning,
not an estimate available from real Canon data. Two CPU intra-op threads.
"""
import argparse
from datetime import datetime,timezone
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
import time
import unittest

import torch

from camera_data import normalize_pair,restore,masked_mse,validate_splits
from camera_unet import CameraUNet,apply


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tensor_sha(x):
    return hashlib.sha256(x.detach().cpu().contiguous().numpy().tobytes()).hexdigest()


def scene(index):
    generator=torch.Generator().manual_seed(9000+index)
    h,w=32,48
    yy,xx=torch.meshgrid(torch.linspace(0,1,h),torch.linspace(0,1,w),indexing='ij')
    base=.1+.55*torch.rand((3,1,1),generator=generator)
    gradient=.25*(xx-.5)+.12*(yy-.5)
    texture=.018*torch.sin((3+index%4)*2*math.pi*xx+.3*index)
    clean=(base+gradient+texture).unsqueeze(0)
    clean[:,:,8:20,12:30]+=.12*torch.rand((1,3,1,1),generator=generator)
    # Deliberately signed/extended fixtures, not claims about physical scene radiance.
    clean[:,:,:3,:3]=-.03
    clean[:,:,-3:,-3:]=1.2
    a=torch.tensor([.0008,.0012,.0016]).reshape(1,3,1,1)
    b=torch.tensor([.0002,.0003,.0004]).reshape(1,3,1,1)
    sigma=(a*clean.clamp_min(0)+b).sqrt()
    noisy=clean+sigma*torch.randn(clean.shape,generator=generator)
    valid=torch.ones((1,1,h,w),dtype=torch.bool)
    valid[:,:,(0,-1),:]=False
    valid[:,:,:,(0,-1)]=False
    role='train' if index<8 else 'validation' if index<12 else 'test'
    identity=dict(id=f'synthetic-{index:02d}',scene_group=f'synthetic-scene-{index:02d}',
                  split=role,content_sha256=tensor_sha(noisy),target_sha256=tensor_sha(clean),
                  source_kind='synthetic_float32_buffer',seed=9000+index)
    x,y,s=normalize_pair(noisy,clean,sigma,2.)
    return dict(identity=identity,x=x,y=y,sigma=s,valid=valid,
                noisy=noisy,clean=clean)


def main(output_dir):
    root=Path(__file__).resolve().parents[2]
    output_dir=output_dir.resolve()
    if not output_dir.is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Output must stay inside dedicated bench build')
    torch.set_num_threads(2);torch.set_num_interop_threads(1)
    torch.manual_seed(71);torch.use_deterministic_algorithms(True)
    start=time.perf_counter()
    log=io.StringIO()
    suite=unittest.defaultTestLoader.discover(str(Path(__file__).parent),pattern='test_camera_data.py')
    checked=unittest.TextTestRunner(stream=log,verbosity=2).run(suite)
    if not checked.wasSuccessful():
        raise RuntimeError(log.getvalue())
    scenes=[scene(i) for i in range(16)]
    counts=validate_splits([s['identity'] for s in scenes])
    model=CameraUNet(width=8)
    optimizer=torch.optim.Adam(model.parameters(),lr=.001)
    losses=[]
    for step in range(64):
        row=scenes[step%8]
        model.train();optimizer.zero_grad(set_to_none=True)
        loss=masked_mse(model(row['x'],row['sigma']),row['y'],row['valid'])
        loss.backward()
        if not all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters()):
            raise ValueError('nonfinite gradient')
        optimizer.step();losses.append(loss.item())
    model.eval()
    metrics=[]
    for row in scenes:
        before=tensor_sha(row['x'])
        output=restore(apply(model,row['x'],row['sigma'],100),2.)
        baseline=masked_mse(row['noisy'],row['clean'],row['valid']).item()
        error=masked_mse(output,row['clean'],row['valid']).item()
        if tensor_sha(row['x'])!=before or not torch.equal(apply(model,row['x'],row['sigma'],0),row['x']):
            raise ValueError('source or zero-identity contract failed')
        delta=output-row['clean']
        mask=row['valid'].expand_as(delta)
        channel_bias=[float(delta[:,c][row['valid'][:,0]].mean()) for c in range(3)]
        metrics.append(dict(**row['identity'],input_mse=baseline,output_mse=error,
                            mse_ratio=error/baseline,channel_mean_error=channel_bias,
                            output_min=float(output.min()),output_max=float(output.max()),
                            valid_channel_samples=int(mask.sum())))
    summaries={}
    for role in counts:
        rows=[r for r in metrics if r['split']==role]
        summaries[role]=dict(scenes=len(rows),mean_input_mse=sum(r['input_mse'] for r in rows)/len(rows),
                             mean_output_mse=sum(r['output_mse'] for r in rows)/len(rows),
                             worst_mse_ratio=max(r['mse_ratio'] for r in rows))
    freeze=dict(architecture='two-level width8 residual CameraUNet',steps=64,lr=.001,
                optimizer='Adam',model_seed=71,scene_seeds=list(range(9000,9016)),
                shared_linear_scale=2.,evaluation_strength=100,
                no_validation_checkpoint_selection=True)
    files=[Path(__file__),Path(__file__).with_name('camera_data.py'),
           Path(__file__).with_name('test_camera_data.py'),Path(__file__).with_name('camera_unet.py')]
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof=dict(schema_version=1,created_utc=stamp,study='synthetic_multiscene_data_pipeline',
               revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
               source_sha256={p.relative_to(root).as_posix():sha(p) for p in files},
               python=platform.python_version(),torch=torch.__version__,device='cpu',
               threads_intra=2,threads_inter=1,freeze=freeze,split_counts=counts,
               data_contract_tests=checked.testsRun,data_contract_test_log=log.getvalue(),
               elapsed_seconds=time.perf_counter()-start,training_losses=losses,
               records=metrics,summary=summaries,
               verdict='technical_pipeline_executed_no_photographic_qualification',
               limits=['One small handcrafted generator; separate seeds are not independent real-world scenes.',
                       'Noise is a Gaussian signal-dependent proxy with ideal sigma from clean data, not Canon calibration.',
                       'No photo, sealed frame, Lightroom reference or source decoder read; no image or weights saved.',
                       '64 fixed steps only; no hyperparameter/checkpoint selection from validation or test.',
                       'Normalization is mathematical inverse within float32 roundoff, not universal bitwise reversibility.',
                       'Reported MSE and biases are synthetic diagnostics, not acceptance gates for Canon quality.',
                       'No GPU, full-frame latency, ONNX or peak-memory qualification.'])
    output_dir.mkdir(parents=True,exist_ok=True)
    destination=output_dir/f'neural-data-pilot-v001-{stamp}.json'
    with destination.open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,indent=2,allow_nan=False)
    print(destination)
    print(json.dumps(dict(tests=checked.testsRun,splits=counts,summary=summaries,
                          elapsed_seconds=proof['elapsed_seconds']),indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,
                        default=Path(__file__).resolve().parents[2]/'build-patch_neural_restoration')
    main(parser.parse_args().output_dir)
