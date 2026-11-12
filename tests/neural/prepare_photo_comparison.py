"""Seal three real full-image sources and compute current RCD/intensity baselines."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys

import numpy as np

from campaign_artifacts import campaign_models
from photo_cfa_tiling import orient_image


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()


def orient_buffer(path,shape,orientation,target):
    if not orientation:return str(path.resolve()),list(shape)
    if target.exists():raise FileExistsError(target)
    raw=np.memmap(path,dtype='<f4',mode='r',shape=tuple(shape))
    oriented=orient_image(raw,orientation)
    out=np.memmap(target,dtype='<f4',mode='w+',shape=oriented.shape)
    for start in range(0,len(oriented),32):out[start:start+32]=oriented[start:start+32]
    out.flush()
    result=(str(target.resolve()),list(oriented.shape))
    del out,oriented,raw
    return result


def run(root,output):
    build=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):raise ValueError('Dedicated output required')
    output.mkdir(parents=True,exist_ok=False)
    source=build/'photo-comparison-v001'
    native=root.parent/'patch_cfa_path_bench/build/raw_bench/Release/iprCfaPathBench.exe'
    models=[]
    for folder in campaign_models(build/'repeatability-control-campaign-v001'):
        proof=json.loads((folder/'result.json').read_text(encoding='utf-8'))
        models.append(dict(seed=proof['seed'],path=str((folder/'weights.pt').resolve()),
            weights_sha256=proof['weights_sha256'],result_sha256=digest(folder/'result.json')))
    plan=dict(schema_version=1,frozen_at_utc=datetime.now(timezone.utc).isoformat(),
        model_selection='First natural repeatability arm, both seeds, chosen by role before photo scores; no best-model selection.',
        models=models,sigma_scenarios=[.01,.03],sigma_scope='Supplied balanced-camera deviations, not calibrated by ISO or matched to classic strength.',
        classic_strengths=[30,60],classic_scope='Current CameraDenoise intensity only; post-tones colour excluded and must be labelled.',
        native=dict(path=str(native.resolve()),sha256=digest(native),revision='473a1a1',dev_parent='8b776bc'),
        display_scope='Shared frozen CameraNeutral/DCP/Rec2020-to-sRGB display snapshot, no independent stretching or optical correction.',
        scenes=[])
    for name,scene in [('6V1A0181','indoor'),('6V1A0263','vegetation'),('6V1A9819','burst_room')]:
        descriptor=source/'full-cfa-sensor-v002'/(name+'.json')
        data=json.loads(descriptor.read_text(encoding='utf-8'))
        full=data['full_balanced_cfa']
        context=source/'contexts-v002'/(name+'.json')
        ctx=json.loads(context.read_text(encoding='utf-8'))
        pixels=descriptor.parent/full['pixels']
        if full['orientation_applied'] or digest(pixels)!=full['pixels_sha256'] or ctx['source_sha256']!=data['source_sha256']:
            raise ValueError('Frozen full sensor input differs')
        plan['scenes'].append(dict(name=name,scene=scene,iso=data['iso'],source_sha256=data['source_sha256'],
            capture=str(descriptor.resolve()),capture_sha256=digest(descriptor),input=str(pixels.resolve()),input_sha256=digest(pixels),
            sensor_shape=full['shape'],pattern=full['pattern'],orientation=data['orientation_flip'],
            context=str(context.resolve()),context_sha256=digest(context)))
    (output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    report=dict(status='started',plan_sha256=digest(output/'plan.json'),scenes=[],
        limits=['Known real development scenes, no clean truth or photographic qualification.','Whole-image RCD before orientation, classic intensity after orientation; repeat1 is not a latency qualification.'])
    try:
        for scene in plan['scenes']:
            folder=output/scene['name'];folder.mkdir()
            height,width,_=scene['sensor_shape']
            pattern=''.join('RGBG'[v] for row in scene['pattern'] for v in row)
            prefix=folder/'parent'
            commands=[str(native),'--input',scene['input'],'--width',str(width),'--height',str(height),
                '--output',str(prefix),'--pattern',pattern,'--kind','cfa','--median','flat','--range','signed','--denoise=','--repeat','1']
            subprocess.run([sys.executable,'tests/neural/run_guarded_native.py','--output',str(folder/'rcd-job'),
                '--hard-mib','4096','--soft-mib','3072','--',*commands],check=True)
            camera,shape=orient_buffer(Path(str(prefix)+'.rcd.f32'),[height,width,3],scene['orientation'],folder/'parent-oriented.f32')
            context=json.loads(Path(scene['context']).read_text(encoding='utf-8'))
            if shape!=context['full_shape']:raise ValueError('Oriented full image extent differs')
            full_rgb=np.memmap(camera,dtype='<f4',mode='r',shape=tuple(shape))
            original=Path(scene['context']).parent/context['pixels']
            checks=[]
            with original.open('rb') as stream:
                for tile in context['tiles']:
                    stream.seek(tile['offset_bytes'])
                    expected=np.frombuffer(stream.read(384*384*3*4),dtype='<f4').reshape(384,384,3)
                    x,y=tile['origin_xy'];actual=full_rgb[y:y+384,x:x+384]
                    difference=float(np.max(np.abs(actual.astype(np.float64)-expected)))
                    checks.append(dict(origin_xy=tile['origin_xy'],bit_exact=bool(np.array_equal(actual,expected)),max_absolute_error=difference))
            del full_rgb
            if not all(c['bit_exact'] for c in checks):raise ValueError('Current RCD does not replay all camera ROIs bit-exactly')
            classic=folder/'classic'
            subprocess.run([sys.executable,'tests/neural/run_guarded_native.py','--output',str(folder/'classic-job'),
                '--hard-mib','4096','--soft-mib','3072','--',str(native),'--input',camera,'--kind','rgb','--width',str(shape[1]),
                '--height',str(shape[0]),'--output',str(classic),'--denoise','30,60','--pixel-scale','1','--repeat','1'],check=True)
            report['scenes'].append(dict(name=scene['name'],camera=camera,shape=shape,camera_sha256=digest(camera),
                rcd_proof=str(prefix)+'.json',classic_proof=str(classic)+'.json',camera_replay=checks,
                classic_outputs=[dict(strength=s,path=str(classic)+f'.denoise-{s}.f32',sha256=digest(str(classic)+f'.denoise-{s}.f32')) for s in (30,60)]))
            (output/'progress.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
            print('BASELINE',scene['name'],flush=True)
        report['status']='completed_three_real_whole_image_baselines_not_qualification'
    except Exception as error:
        report.update(status='stopped_on_error',error=str(error));raise
    finally:
        (output/'progress.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(Path(__file__).resolve().parents[2],args.output)
