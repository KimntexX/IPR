"""Apply unchanged sensor-frame RCD and common DCP display to local-sigma outputs."""
from datetime import datetime,timezone
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from prepare_photo_comparison import digest,orient_buffer


def run(source=None,output=None):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    source=source or build/'local-sigma-inference-v001/result.json'
    output=output or build/'local-sigma-display-v001'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (source,output)):raise ValueError('Dedicated display paths required')
    proof=json.loads(source.read_text(encoding='utf-8'))
    if proof['status']!='completed_local_sigma_inference_not_qualification':raise ValueError('Complete inference required')
    if shutil.disk_usage(build).free<8*1024**3:raise RuntimeError('Insufficient space for owned comparison buffers')
    plan=proof['plan'];native=plan['native']['path'];renderer=build/'native/Release/iprNeuralDisplayCapture.exe'
    if digest(native)!=plan['native']['sha256']:raise ValueError('RCD comparator changed')
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',inference_sha256=digest(source),renderer_sha256=digest(renderer),records=[],
        scope='Same whole sensor RCD, orientation and frozen shared CameraNeutral/DCP display. Known photos, no clean truth or full-current-production claim.')
    def execute(job,command):
        subprocess.run([sys.executable,'tests/neural/run_guarded_native.py','--output',str(job),
            '--hard-mib','4096','--soft-mib','3072','--',*map(str,command)],check=True)
    try:
        for record in proof['records']:
            scene=next(s for s in plan['scenes'] if s['name']==record['scene'])
            if digest(record['path'])!=record['sha256'] or digest(scene['context'])!=scene['context_sha256']:
                raise ValueError('Input or display context changed')
            label=f"{scene['name']}-seed{record['seed']}-local"
            prefix=output/label;h,w,_=scene['sensor_shape'];pattern=''.join('RGBG'[c] for row in scene['pattern'] for c in row)
            execute(output/(label+'-rcd-job'),[native,'--input',record['path'],'--width',w,'--height',h,'--output',prefix,
                '--pattern',pattern,'--kind','cfa','--median','flat','--range','signed','--denoise=','--repeat','1'])
            camera,shape=orient_buffer(Path(str(prefix)+'.rcd.f32'),[h,w,3],scene['orientation'],output/(label+'-oriented.f32'))
            expected=json.loads(Path(scene['context']).read_text(encoding='utf-8'))['full_shape']
            if shape!=expected:raise ValueError('Display geometry differs')
            display=output/(label+'-srgb')
            execute(output/(label+'-display-job'),[renderer,'--input',camera,'--context',scene['context'],'--output',display])
            rendered=json.loads(Path(str(display)+'.json').read_text(encoding='utf-8'))
            result['records'].append(dict(scene=scene['name'],seed=record['seed'],display=rendered,rcd_proof=str(prefix)+'.json'))
            (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
            print('DISPLAY LOCAL',label,flush=True)
        result['status']='completed_local_sigma_display_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-local-sigma-display-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source',type=Path);parser.add_argument('--output',type=Path)
    args=parser.parse_args();run(args.source,args.output)
