"""Owned sequential curve supervisor, fixed horizon and bounded process-tree cleanup."""
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'sensor-learning-campaign-v001';output.mkdir(exist_ok=False)
    plan_path=build/'sensor-learning-plan-v001/plan.json';plan=json.loads(plan_path.read_text(encoding='utf-8'))
    preflights=[json.loads((build/name/'result.json').read_text(encoding='utf-8')) for name in
        ('sensor-curve-preflight-seed71-v003','sensor-curve-preflight-seed73-v003')]
    if any(p['status']!='passed_sensor_curve_preflight_discarded' or not p['resume_bit_exact'] for p in preflights):
        raise ValueError('Both discarded engineering preflights must pass')
    if any(any(sha256(Path(__file__).with_name(n))!=h for n,h in p['tools'].items()) for p in preflights):
        raise ValueError('Preflight no longer matches training code')
    if max(p['median_seconds_per_update'] for p in preflights)*4096>3600:
        raise RuntimeError('Measured training projection exceeds one-hour admission budget')
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';cpu=root/'.venv-neural/Scripts/python.exe'
    images=Path('C:/Users/victo/AppData/Local/Python/pythoncore-3.14-64/python.exe')
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan_sha256=sha256(plan_path),stages=[],script_sha256=sha256(Path(__file__)),
        tools={p:sha256(Path(__file__).with_name(p)) for p in ('train_sensor_curve.py','sensor_curve_noise.py','infer_sensor_sigma.py','display_local_sigma.py','prepare_curve_photos.py','summarize_sensor_curve.py')},
        policy='Two seeds sequentially, all declared milestones, same known photo controls. No gate relaxation, new reserve, publication, merge, tag or sleep.')
    deadline=time.monotonic()+plan['config']['maximum_campaign_seconds']
    def write():
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'progress.json')
    def command(name,python,script,*args):
        if any(sha256(Path(__file__).with_name(n))!=h for n,h in result['tools'].items()):raise ValueError('Campaign code changed')
        row=dict(name=name,started_at_utc=datetime.now(timezone.utc).isoformat(),command=[str(python),str(root/'tests/neural'/script),*map(str,args)])
        result['stages'].append(row);result['status']=name;write()
        with (output/(name+'.log')).open('x',encoding='utf-8') as log:
            child=subprocess.Popen(row['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                while True:
                    remaining=deadline-time.monotonic()
                    if remaining<=0:raise TimeoutError('Fixed90minute campaign budget reached')
                    try:code=child.wait(timeout=min(60.,remaining));break
                    except subprocess.TimeoutExpired:write()
            except BaseException:
                try:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=False)
                finally:
                    if child.poll() is None:child.kill()
                    child.wait(timeout=15)
                raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());write()
        if code:raise RuntimeError('Stage failed: '+name)
    try:
        for seed in (71,73):command(f'train-{seed}',gpu,'train_sensor_curve.py','--seed',seed,'--output',output/f'seed{seed}')
        command('prepare-all-photo-milestones',cpu,'prepare_curve_photos.py')
        for step in (256,1024,2048):
            command(f'infer-{step}',gpu,'infer_sensor_sigma.py','--plan',output/f'photo-plans/step{step}/plan.json','--output',output/f'photo-inference-{step}')
            command(f'display-{step}',cpu,'display_local_sigma.py','--source',output/f'photo-inference-{step}/result.json','--output',output/f'photo-display-{step}')
        command('all-known-photo-descriptors',images,'summarize_sensor_curve.py')
        result['status']='completed_bounded_sensor_curve_known_only'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-sensor-curve-campaign-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':run()
