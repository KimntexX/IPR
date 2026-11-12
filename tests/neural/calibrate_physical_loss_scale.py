"""Freeze one overall normalized-loss scale from training-only noisy sigma maps.

No network weights, gradients, development images or quality scores are used.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import time
from prepare_next_training import read,write,digest


def run(output):
    import numpy as np
    import torch
    from memory_budget import MemoryGuard
    from physical_natural_training import NoiseModel,ROOT,BUILD
    from physical_training_objective import fixed_sigma_scale
    parent=BUILD/'physical-natural-training-v005'
    plan,data=read(parent/'plan.json'),read(parent/'data.json')
    for name,expected in data['files'].items():
        if digest(parent/name)!=expected:raise ValueError('Parent arrays changed')
    output.mkdir(exist_ok=False)
    now=datetime.now(timezone.utc)
    result=dict(schema_version=1,data_version='v001',campaign_id=output.name,measured_at_utc=now.isoformat(),
        status='started',parent_plan_sha256=digest(parent/'plan.json'),seed=941071,batches=32,batch=8,
        sigma_floor=1/plan['target_domain']['divisor'],rows=[],
        rule='k = N / sum(1/max(sigma_noisy,floor)); fixed global scalar, no per-batch normalization',
        source_policy='Uniform balanced ISO3200/6400/16000/51200; patches from training.npy and calibration darks only',
        sources=dict(input_hashes=data['files']),
        tools={name:digest(ROOT/'tests/neural'/name) for name in ('calibrate_physical_loss_scale.py',
            'physical_training_objective.py','physical_natural_training.py','memory_budget.py')},
        limits=['Average pixel weight1 on calibration samples does not guarantee matching parameter-gradient norms',
                'Not a photographic qualification or automatic approval for long training'])
    write(output/'freeze.json',result)
    guard=MemoryGuard(hard_bytes=3*1024**3,soft_bytes=2*1024**3,system_fraction=.92)
    started=time.monotonic()
    try:
        with guard:
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:
                raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5)
            training=np.load(parent/'training.npy',mmap_mode='r',allow_pickle=False)
            noise=NoiseModel(parent,plan,'cuda')
            rng=torch.Generator(device='cuda').manual_seed(941071)
            total=0.;count=0
            with torch.inference_mode():
                for batch in range(32):
                    guard.check()
                    if time.monotonic()-started>300:raise TimeoutError('Loss calibration wall budget reached')
                    indices=torch.randint(data['training_tiles'],(8,),generator=rng,device='cuda').tolist()
                    iso_ids=torch.arange(4,device='cuda').repeat(2)
                    choices=torch.randint(1<<30,(8,),generator=rng,device='cuda').tolist()
                    dark=torch.stack([noise.dark(int(i),'calibration',choice) for i,choice in zip(iso_ids.tolist(),choices)])
                    targets=torch.from_numpy(np.stack([training[i] for i in indices])).cuda()
                    _,sigma=noise.sample(targets,iso_ids,dark,rng)
                    inverse=sigma.clamp_min(result['sigma_floor']).double().reciprocal()
                    partial=float(inverse.sum());total+=partial;count+=inverse.numel()
                    result['rows'].append(dict(batch=batch,training_tile_indices=indices,iso_ids=iso_ids.tolist(),
                        calibration_dark_choices=choices,inverse_sigma_sum=partial,pixel_count=inverse.numel(),
                        floor_active_pixels=int((sigma<result['sigma_floor']).sum())))
                    write(output/'progress.json',dict(status='running',batches=batch+1))
            result.update(status='completed_training_only_loss_scale_calibration',objective_scale=fixed_sigma_scale(total,count),
                inverse_sigma_sum=total,pixel_count=count,torch_version=str(torch.__version__),numpy_version=np.__version__)
    except Exception as error:
        result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        write(output/'result.json',result);write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-loss-scale-calibration-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(args.output.resolve())
