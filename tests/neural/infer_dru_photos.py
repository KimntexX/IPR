"""Last declared CFA checkpoint on all three known whole-sensor photo controls."""
import argparse
from datetime import datetime,timezone
import gc
import json
import os
from pathlib import Path
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run(campaign):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not campaign.resolve().is_relative_to(build.resolve()):raise ValueError('Owned campaign required')
    output=campaign/'photo-inference';output.mkdir(exist_ok=False)
    plan=json.loads((build/'local-sigma-plan-v001/plan.json').read_text(encoding='utf-8'))
    result=dict(schema_version=1,status='started',plan=plan,records=[],engineering=[],
        scope='From-scratch CFA bootstrap, last declared checkpoint per seed, known photos only; no clean truth or production qualification.')
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92)
    try:
        with guard:
            import numpy as np
            import torch
            from dru_cfa import create
            from photo_cfa_tiling import tiled_correct
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            torch.cuda.set_per_process_memory_fraction(.5);torch.use_deterministic_algorithms(True)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            result['plan']['models']=[]
            for seed in (71,73):
                folder=campaign/f'seed{seed}';progress=json.loads((folder/'result.json').read_text(encoding='utf-8'))
                checkpoints=[r for r in progress['milestones'] if r['step']>0]
                if not checkpoints:raise ValueError('No nonzero declared milestone for seed '+str(seed))
                ref=max(checkpoints,key=lambda x:x['step']);path=Path(ref['path'])
                if sha256(path)!=ref['sha256']:raise ValueError('Checkpoint changed')
                state=torch.load(path,weights_only=True,map_location='cpu')
                model,provenance=create(root,seed);model.load_state_dict(state['model_state'],strict=True)
                del state;gc.collect();model=model.cuda().eval().requires_grad_(False)
                result['plan']['models'].append(dict(seed=seed,step=ref['step'],path=str(path),weights_sha256=ref['sha256'],architecture=provenance))
                for scene in plan['scenes']:
                    guard.check()
                    if sha256(Path(scene['input']))!=scene['input_sha256'] or sha256(Path(scene['sigma_path']))!=scene['sigma_sha256']:
                        raise ValueError('Photo/sigma input changed')
                    h,w,_=scene['sensor_shape'];source=np.memmap(scene['input'],dtype='<f4',mode='r',shape=(h,w))
                    sigma=np.load(scene['sigma_path'],mmap_mode='r',allow_pickle=False)
                    target=output/f"{scene['name']}-seed{seed}.f32"
                    destination=np.memmap(target,dtype='<f4',mode='w+',shape=(h,w))
                    geometry=tiled_correct(model,source,scene['pattern'],sigma,destination,'cuda',halo=384,batch=1,check=guard.check)
                    destination.flush();del destination,source,sigma;gc.collect();torch.cuda.empty_cache()
                    result['records'].append(dict(scene=scene['name'],seed=seed,step=ref['step'],path=str(target.resolve()),sha256=sha256(target),geometry=geometry))
                    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
                    print('DRU PHOTO',seed,scene['name'],flush=True)
                del model;gc.collect();torch.cuda.empty_cache()
            result['status']='completed_local_sigma_inference_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['memory']=guard.stats()
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-photos-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--campaign',type=Path,required=True);run(p.parse_args().campaign)
