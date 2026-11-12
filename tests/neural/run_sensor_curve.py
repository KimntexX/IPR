"""Owned sequential curve supervisor, fixed horizon and bounded process-tree cleanup."""
from datetime import datetime,timezone
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256


def run(resume=False):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'sensor-learning-campaign-v001';output.mkdir(exist_ok=resume)
    previous=json.loads((output/'progress.json').read_text(encoding='utf-8')) if resume else None
    if previous is not None and previous['status']!='stopped_on_error':raise ValueError('Resume only an explicitly stopped campaign')
    if previous is not None and (not previous['stages'] or not previous['stages'][-1]['name'].startswith('train-')):
        raise ValueError('Automatic resume supports interrupted training only, before photo stages')
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
    spent=0.
    if previous is not None:
        spent=previous.get('prior_active_seconds',0.)+(datetime.fromisoformat(previous['finished_at_utc'])-datetime.fromisoformat(previous['started_at_utc'])).total_seconds()
        result.update(prior_attempt=previous,prior_active_seconds=spent,prior_progress_sha256=sha256(output/'progress.json'))
    remaining_budget=plan['config']['maximum_campaign_seconds']-spent
    if remaining_budget<=0:raise TimeoutError('Original active campaign budget exhausted')
    started=time.monotonic();deadline=started+remaining_budget
    attempt_stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    def write():
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'progress.json')
    def command(name,python,script,*args):
        if any(sha256(Path(__file__).with_name(n))!=h for n,h in result['tools'].items()):raise ValueError('Campaign code changed')
        row=dict(name=name,started_at_utc=datetime.now(timezone.utc).isoformat(),command=[str(python),str(root/'tests/neural'/script),*map(str,args)])
        result['stages'].append(row);result['status']=name;write()
        with (output/(name+('-'+attempt_stamp if resume else '')+'.log')).open('x',encoding='utf-8') as log:
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
        for seed in (71,73):
            folder=output/f'seed{seed}'
            if folder.exists():
                state=json.loads((folder/'result.json').read_text(encoding='utf-8'))
                if state['status']=='completed_sensor_curve_continuation_not_quality_qualification':
                    result['stages'].append(dict(name=f'train-{seed}',reused_completed_result_sha256=sha256(folder/'result.json')));write();continue
                if not resume or not (folder/'resume.pt').exists():raise ValueError('No valid resume checkpoint')
                command(f'train-{seed}',gpu,'train_sensor_curve.py','--seed',seed,'--output',folder,'--resume')
            else:command(f'train-{seed}',gpu,'train_sensor_curve.py','--seed',seed,'--output',folder)
        command('prepare-all-photo-milestones',cpu,'prepare_curve_photos.py')
        for step in (256,1024,2048):
            command(f'infer-{step}',gpu,'infer_sensor_sigma.py','--plan',output/f'photo-plans/step{step}/plan.json','--output',output/f'photo-inference-{step}')
            command(f'display-{step}',cpu,'display_local_sigma.py','--source',output/f'photo-inference-{step}/result.json','--output',output/f'photo-display-{step}')
        command('all-known-photo-descriptors',images,'summarize_sensor_curve.py')
        result['status']='completed_bounded_sensor_curve_known_only'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),attempt_active_seconds=time.monotonic()-started);write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-sensor-curve-campaign-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--resume',action='store_true');run(parser.parse_args().resume)
