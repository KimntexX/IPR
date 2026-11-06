"""Owned overnight sequence; no chat polling, bounded deadlines and process-tree cleanup."""
import argparse
import ctypes
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from campaign_artifacts import sha256
from memory_budget import snapshot


def run(plan_path,preflight,output,training_only=False):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (plan_path,preflight,output)):
        raise ValueError('Owned paths required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'));proof=json.loads(preflight.read_text(encoding='utf-8'))
    admission=plan.get('admission_fraction',.80);stable_seconds=plan.get('admission_stable_seconds',20)
    retries=plan.get('maximum_memory_retries',2)
    if not 0<admission<=.80 or stable_seconds<20 or retries not in (0,1,2):raise ValueError('Invalid admission/retry policy')
    if proof['status']!='passed_dru_training_preflight_discarded' or not proof.get('resume_bit_exact') or proof['plan_sha256']!=sha256(plan_path):
        raise ValueError('Successful actual-loop preflight for exactly this plan required')
    if shutil.disk_usage(build).free<16*1024**3:raise RuntimeError('At least16GiB free required')
    if any(sha256(root/'tests/neural'/n)!=h for n,h in plan['tools'].items()):raise ValueError('Sealed sources changed')
    output.mkdir(exist_ok=False)
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';cpu=root/'.venv-neural/Scripts/python.exe'
    images=Path('C:/Users/victo/AppData/Local/Python/pythoncore-3.14-64/python.exe')
    deadline=datetime.fromisoformat(plan['campaign_deadline_utc']).timestamp()
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan=plan,plan_sha256=sha256(plan_path),preflight_sha256=sha256(preflight),stages=[],pid=os.getpid(),
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('run_dru_overnight.py','infer_dru_photos.py','display_local_sigma.py','summarize_dru_overnight.py')},
        training_only=training_only,
        policy='Both seeds sequentially, last declared checkpoints, no model selection, no new reserve, no publication or sleep. Training-only mode defers photo calculations and assessment.')
    power=ctypes.WinDLL('kernel32',use_last_error=True)
    power.SetThreadExecutionState.argtypes=[ctypes.c_uint];power.SetThreadExecutionState.restype=ctypes.c_uint
    if not power.SetThreadExecutionState(0x80000001):raise ctypes.WinError(ctypes.get_last_error())
    def write():
        temporary=output/'progress.tmp';temporary.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temporary.replace(output/'progress.json')
    def wait_memory():
        stable=None;started=time.monotonic()
        while True:
            if time.time()>=deadline or time.monotonic()-started>1200:raise TimeoutError('Memory admission timeout')
            if (output/'STOP').exists():raise InterruptedError('Requested campaign stop')
            sample=snapshot();result['admission_memory']=sample;write()
            if sample['system_used_fraction']<admission:
                if stable is None:stable=time.monotonic()
                if time.monotonic()-stable>=stable_seconds:return
            else:stable=None
            time.sleep(10)
    def command(name,python,script,*args):
        if (output/'STOP').exists():raise InterruptedError('Requested campaign stop')
        if any(sha256(root/'tests/neural'/n)!=h for n,h in result['tools'].items()):raise ValueError('Orchestration source changed')
        wait_memory()
        row=dict(name=name,started_at_utc=datetime.now(timezone.utc).isoformat(),command=[str(python),str(root/'tests/neural'/script),*map(str,args)])
        result['status']=name;result['stages'].append(row);write()
        with (output/(name+'.log')).open('x',encoding='utf-8') as log:
            child=subprocess.Popen(row['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            row['pid']=child.pid;write()
            try:
                while True:
                    remaining=deadline-time.time()
                    if remaining<=0:raise TimeoutError('Morning campaign deadline reached')
                    try:code=child.wait(timeout=min(60.,remaining));break
                    except subprocess.TimeoutExpired:write()
            except BaseException:
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=False)
                if child.poll() is None:child.kill()
                child.wait(timeout=15);raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());write()
        return code
    try:
        for seed in plan['seeds']:
            folder=output/f'seed{seed}';attempt=0
            while True:
                args=['--plan',plan_path,'--output',folder,'--seed',seed]
                if attempt:args.append('--resume')
                code=command(f'train-{seed}-attempt{attempt+1}',gpu,'train_dru_cfa.py',*args)
                current=json.loads((folder/'result.json').read_text(encoding='utf-8'))
                if code==0 and current['status'] in ('completed_dru_bootstrap_not_photographic_qualification','paused_dru_training'):break
                if (current.get('memory',{}).get('reason') not in ('process_soft_memory_limit','system_physical_memory_threshold')
                        or not (folder/'resume.json').exists() or attempt>=retries):
                    raise RuntimeError('Training failed: '+str(seed))
                attempt+=1
            if current['status']=='paused_dru_training' and current['completed_steps']==0:raise RuntimeError('No trained checkpoint')
        if training_only:
            result['status']='completed_dru_overnight_training_photos_and_assessment_deferred'
        else:
            if command('photo-inference',gpu,'infer_dru_photos.py','--campaign',output):raise RuntimeError('Photographic inference failed')
            if command('same-rcd-display',cpu,'display_local_sigma.py','--source',output/'photo-inference/result.json','--output',output/'photo-display'):
                raise RuntimeError('RCD/display failed')
            if command('assessment',images,'summarize_dru_overnight.py','--campaign',output):raise RuntimeError('Assessment failed')
            result['status']='completed_dru_overnight_known_bootstrap_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        power.SetThreadExecutionState(0x80000000)
        result['known_controls']=[]
        result['memory_traces']=[]
        for seed in plan['seeds']:
            for path in sorted((output/f'seed{seed}').glob('control-*.json')):
                result['known_controls'].append(dict(seed=seed,path=str(path.resolve()),sha256=sha256(path),
                    measurements=json.loads(path.read_text(encoding='utf-8'))))
            for path in sorted((output/f'seed{seed}').glob('memory-*.jsonl')):
                result['memory_traces'].append(dict(seed=seed,path=str(path.resolve()),sha256=sha256(path),
                    records=[json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()]))
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-overnight-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('plan','preflight','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--training-only',action='store_true')
    a=p.parse_args();run(a.plan,a.preflight,a.output,a.training_only)
