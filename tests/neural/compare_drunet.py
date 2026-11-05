"""Bounded official DRUNet sRGB comparator; never camera/CFA training data."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import sys
import subprocess
import time

os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG', ':4096:8')
from memory_budget import MemoryGuard
from campaign_artifacts import sha256 as digest


def run(display, output, image_python):
    root = Path(__file__).resolve().parents[2]
    build = root / 'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):
        raise ValueError('Dedicated output required')
    output.mkdir(parents=True, exist_ok=False)
    external = build / 'third-party-drunet-v001'
    manifest = json.loads((external / 'manifest.json').read_text(encoding='utf-8'))
    for item in manifest['files']:
        if digest(external / item['path']) != item['sha256']:
            raise ValueError('Official source changed')
    weights = Path(manifest['weight']['path'])
    if digest(weights) != manifest['weight']['sha256']:
        raise ValueError('Official weights changed')
    parent = json.loads((display / 'result.json').read_text(encoding='utf-8'))
    if not parent['status'].startswith('completed_'):
        raise ValueError('Display incomplete')
    guard = MemoryGuard(hard_bytes=6*1024**3, soft_bytes=4608*1024**2, system_fraction=.92)
    result = dict(schema_version=1, status='started', manifest=manifest, records=[],
                  display_proof_sha256=digest(display / 'result.json'),
                  scope='Official AWGN sRGB comparator on known development photos, not sensor-calibrated or ground truth; no training.',
                  sigma_8bit=[15,25], halo=192, output_side=384)
    try:
        with guard:
            import numpy as np
            import torch
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0] < 4*1024**3:
                raise RuntimeError('CUDA unavailable or busy; no unguarded fallback')
            torch.cuda.set_per_process_memory_fraction(.5)
            torch.backends.cuda.matmul.allow_tf32=False
            torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False
            torch.use_deterministic_algorithms(True)
            sys.path.insert(0, str(external.resolve()))
            from models.network_unet import UNetRes
            model=UNetRes(in_nc=4,out_nc=3,nc=[64,128,256,512],nb=4,act_mode='R',
                          downsample_mode='strideconv',upsample_mode='convtranspose')
            model.load_state_dict(torch.load(weights,map_location='cpu',weights_only=True),strict=True)
            model=model.eval().requires_grad_(False).to('cuda')
            result.update(torch_version=str(torch.__version__),parameters=sum(p.numel() for p in model.parameters()))
            with torch.inference_mode():
                for scene in parent['scenes']:
                    variant=next(v for v in scene['variants'] if v['halo']==192)
                    proof=json.loads(Path(variant['display']).read_text(encoding='utf-8'))
                    for roi in proof['rois']:
                        guard.check()
                        path=Path(roi['path'])
                        if digest(path)!=roi['sha256']:
                            raise ValueError('Display pixels changed')
                        payload=subprocess.check_output([str(image_python),'-c',
                            'from PIL import Image; import sys; im=Image.open(sys.argv[1]).convert("RGB"); '
                            'assert im.size==(768,768); sys.stdout.buffer.write(im.tobytes())',str(path)],timeout=30)
                        array=np.frombuffer(payload,dtype=np.uint8).reshape(768,768,3).astype(np.float32)/255
                        if array.shape!=(768,768,3):
                            raise ValueError('Missing fixed context')
                        image=torch.from_numpy(array.transpose(2,0,1).copy())[None].to('cuda')
                        for sigma in (15,25):
                            noise=torch.full((1,1,768,768),sigma/255,device='cuda')
                            torch.cuda.synchronize();started=time.perf_counter()
                            prediction=model(torch.cat((image,noise),1))
                            torch.cuda.synchronize();elapsed=time.perf_counter()-started
                            if not torch.isfinite(prediction).all():
                                raise ValueError('Nonfinite official comparator')
                            crop=prediction[0,:,192:576,192:576].permute(1,2,0).cpu().numpy()
                            target=output/f"{scene['name']}-roi{roi['roi']}-sigma{sigma}.png"
                            pixels=np.rint(np.clip(crop,0,1)*255).astype(np.uint8).tobytes()
                            subprocess.run([str(image_python),'-c',
                                'from PIL import Image; import sys; '
                                'Image.frombytes("RGB",(384,384),sys.stdin.buffer.read()).save(sys.argv[1])',str(target)],
                                input=pixels,check=True,timeout=30)
                            result['records'].append(dict(scene=scene['name'],roi=roi['roi'],sigma_8bit=sigma,
                                source_sha256=roi['sha256'],path=str(target),sha256=digest(target),elapsed_seconds=elapsed))
                    print('DRUNET',scene['name'],flush=True)
            result.update(status='completed_official_srgb_comparator_not_qualification',
                          peak_cuda_allocated_bytes=torch.cuda.max_memory_allocated())
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error))
        raise
    finally:
        result['memory']=guard.stats()
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        (root/'docs/validation/raw'/f'neural-drunet-comparison-v001-{stamp}.json').write_text(
            json.dumps(result,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--display',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--image-python',type=Path,required=True,help='Existing Pillow interpreter; no environment changes')
    args=parser.parse_args();run(args.display,args.output,args.image_python)
