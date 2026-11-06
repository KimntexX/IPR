"""Bounded sequential data-policy comparison, admitted preflights, no automatic retries."""
import argparse
import ctypes
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256
from memory_budget import snapshot


def run(plan_path,output):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (plan_path,output)):raise ValueError('Owned campaign required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if plan['status']!='sealed_matched_dru_data_comparison':raise ValueError('Sealed matched protocol required')
    output.mkdir(exist_ok=False)
    deadline=datetime.fromisoformat(plan['campaign_deadline_utc']).timestamp()
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';cpu=root/'.venv-neural/Scripts/python.exe'
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        pid=os.getpid(),plan=plan,plan_sha256=sha256(plan_path),stages=[])
    power=ctypes.WinDLL('kernel32',use_last_error=True)
    power.SetThreadExecutionState.argtypes=[ctypes.c_uint];power.SetThreadExecutionState.restype=ctypes.c_uint
    if not power.SetThreadExecutionState(0x80000001):raise ctypes.WinError(ctypes.get_last_error())
    def write():
        temporary=output/'progress.tmp';temporary.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temporary.replace(output/'progress.json')
    def check():
        if time.time()>=deadline:raise TimeoutError('Fixed data-comparison deadline')
        if (output/'STOP').exists():raise InterruptedError('Comparison stop requested')
        if any(sha256(root/'tests/neural'/n)!=h for n,h in plan['supervisor_tools'].items()):raise ValueError('Supervisor source changed')
    def admit():
        stable=None;started=time.monotonic()
        while True:
            check()
            if time.monotonic()-started>1200:raise TimeoutError('Memory admission timeout')
            sample=snapshot();result['admission_memory']=sample;write()
            if sample['system_used_fraction']<.70:
                if stable is None:stable=time.monotonic()
                if time.monotonic()-stable>=60:return
            else:stable=None
            time.sleep(10)
    def command(name,python,script,*args):
        check();row=dict(name=name,command=[str(python),str(root/'tests/neural'/script),*map(str,args)],started_at_utc=datetime.now(timezone.utc).isoformat())
        result['stages'].append(row);result['status']=name;write()
        with (output/(name+'.log')).open('x',encoding='utf-8') as log:
            child=subprocess.Popen(row['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            row['pid']=child.pid;write()
            try:
                while True:
                    check()
                    try:code=child.wait(timeout=min(60.,max(.1,deadline-time.time())));break
                    except subprocess.TimeoutExpired:write()
            except BaseException:
                subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=False)
                if child.poll() is None:child.kill()
                child.wait(timeout=15);raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());write()
        if code:raise RuntimeError('Comparison stage failed: '+name)
    try:
        for arm in plan['arms']:
            path=Path(arm['path']);name=arm['name']
            if sha256(path)!=arm['sha256']:raise ValueError('Arm plan changed')
            admit()
            probe=output/(name+'-preflight')
            command(name+'-preflight',gpu,'train_dru_cfa.py','--plan',path,'--output',probe,'--seed',71,'--preflight')
            command(name+'-training',cpu,'run_dru_overnight.py','--plan',path,'--preflight',probe/'result.json','--output',output/name,'--training-only')
        command('paired-assessment',cpu,'summarize_dru_data_comparison.py','--campaign',output)
        result['status']='completed_matched_dru_data_comparison'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        power.SetThreadExecutionState(0x80000000)
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-data-comparison-supervisor-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.plan,a.output)
