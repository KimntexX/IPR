"""Separate learned reconstruction diagnostic, never confused with corrected-CFA RCD."""
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess

os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from memory_budget import MemoryGuard
from campaign_artifacts import sha256


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    folder=build/'learned-photo-diagnostic-v001';folder.mkdir(exist_ok=False)
    plan_path=build/'real-photo-baselines-v001/plan.json'
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    baseline=json.loads((build/'real-photo-baselines-v001/progress.json').read_text(encoding='utf-8'))
    renderer=build/'native/Release/iprNeuralDisplayCapture.exe'
    # Explicit existing CPU runner; neither pinned neural environment is changed.
    image_python=Path('C:/Users/victo/AppData/Local/Python/pythoncore-3.14-64/python.exe')
    result=dict(schema_version=1,status='started',plan_sha256=sha256(plan_path),
        renderer_sha256=sha256(renderer),records=[],replays=[],sigma=.03,
        scope='Separate learned RGB reconstruction on 9 fixed ROIs per scene, context648 halo>=128. Not full-image inference, RCD replacement qualification, clean truth or model selection.')
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=4608*1024**2,system_fraction=.92)
    def render(path,context,prefix,tiles):
        command=[str(renderer),'--input',str(path),'--context',context,'--output',str(prefix)]
        if tiles:command+=['--tiles']
        subprocess.run([str(image_python),'tests/neural/run_guarded_native.py','--output',str(prefix)+'-job',
            '--hard-mib','4096','--soft-mib','3072','--',*command],check=True)
        return json.loads(Path(str(prefix)+'.json').read_text(encoding='utf-8'))
    try:
        with guard:
            import numpy as np
            import torch
            from torch.nn import functional as F
            from bayer_canonical import predict
            from edge_protected_noise import EdgeProtectedNoisyUNet
            from photo_cfa_tiling import orient_image
            torch.set_num_threads(4);torch.set_num_interop_threads(1)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:
                raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.use_deterministic_algorithms(True)
            for scene in plan['scenes']:
                parent=next(r for r in baseline['scenes'] if r['name']==scene['name'])
                replay=render(parent['camera'],scene['context'],folder/(scene['name']+'-neutral-replay'),False)
                original=json.loads((build/'real-photo-display-v002'/scene['name']/'RCD-neutral-srgb.json').read_text(encoding='utf-8'))
                if [r['sha256'] for r in replay['rois']]!=[r['sha256'] for r in original['rois']]:
                    raise ValueError('Renderer extension changed neutral display')
                result['replays'].append(dict(scene=scene['name'],pngs_bit_exact=9))
            for ref in plan['models']:
                if sha256(Path(ref['path']))!=ref['weights_sha256']:raise ValueError('Weights changed')
                model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False).to('cuda')
                model.load_state_dict(torch.load(ref['path'],weights_only=True,map_location='cpu')['state_dict'],strict=True)
                for scene in plan['scenes']:
                    guard.check()
                    if sha256(Path(scene['input']))!=scene['input_sha256']:raise ValueError('Input changed')
                    h,w,_=scene['sensor_shape'];bits=scene['orientation']
                    source=np.memmap(scene['input'],dtype='<f4',mode='r',shape=(h,w))
                    context=json.loads(Path(scene['context']).read_text(encoding='utf-8'))
                    label=f"{scene['name']}-learned-rgb-seed{ref['seed']}"
                    tiles_path=folder/(label+'.f32')
                    with tiles_path.open('xb') as stream,torch.inference_mode():
                        for tile in context['tiles']:
                            x,y=tile['origin_xy']
                            if bits&4:x,y=y,x
                            if bits&1:x=w-x-384
                            if bits&2:y=h-y-384
                            left=(x-128)//8*8;top=(y-128)//8*8
                            if left<0 or top<0 or left+648>w or top+648>h:raise ValueError('Diagnostic context outside sensor')
                            values=torch.from_numpy(np.array(source[top:top+648,left:left+648],copy=True))[None,None].to('cuda')/2
                            packed=F.pixel_unshuffle(values,2)
                            tags=torch.tensor([np.array(scene['pattern']).reshape(-1).tolist()],device='cuda')
                            prediction=predict(model,packed,torch.full_like(packed,.03/2),tags,False,True)*2
                            rgb=prediction[0,:,y-top:y-top+384,x-left:x-left+384].permute(1,2,0).cpu().numpy()
                            rgb=orient_image(rgb,bits)
                            if rgb.shape!=(384,384,3) or not np.isfinite(rgb).all():raise ValueError('Invalid learned RGB')
                            stream.write(rgb.astype('<f4').tobytes())
                    display=render(tiles_path,scene['context'],folder/label,True)
                    result['records'].append(dict(scene=scene['name'],seed=ref['seed'],weights_sha256=ref['weights_sha256'],
                        tiles_sha256=sha256(tiles_path),display=display))
                    del source
                    print('LEARNED-DIAGNOSTIC',label,flush=True)
            result['status']='completed_separate_learned_reconstruction_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['memory']=guard.stats()
        (folder/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        (root/'docs/validation/raw'/f'neural-learned-photo-diagnostic-v001-{stamp}.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':run()
