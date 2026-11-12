"""Independent decision-point watcher; preserve checkpoints before user-authorized sleep."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256
from scheduled_comparison_results import assess
from windows_sleep import process_identity,input_idle_seconds,probe_sleep_permission,suspend


def decision(alive,now,deadline,promising):
    if not alive:return 'stop_and_sleep'
    if now<deadline:return 'wait'
    return 'continue_promising' if promising else 'stop_and_sleep'


def evidence(state,comparison):
    found=[]
    for entry in state.get('assessments',[]):
        value=entry['assessment']
        if value['promising_candidates'] or value['strict_candidates']:found.append(value)
    # A finished pair may be useful before the last arm has completed the initial comparison.
    for arm in ('noise','details'):
        folders={name:comparison/name for name in ('reference',arm)}
        try:
            if all(json.loads((folder/f'seed{seed}/result.json').read_text(encoding='utf-8'))['status']=='completed_dru_bootstrap_not_photographic_qualification'
                    for folder in folders.values() for seed in (71,73)):
                result=assess(folders,256)
                if result['promising_candidates'] or result['strict_candidates']:found.append(result)
        except (FileNotFoundError,ValueError,KeyError):pass
    return found


def run(config_path,output):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    config=json.loads(config_path.read_text(encoding='utf-8'));output.mkdir(exist_ok=False)
    scheduler=Path(config['scheduler']);comparison=Path(config['comparison']);deadline=datetime.fromisoformat(config['decision_utc']).timestamp()
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (scheduler,comparison,output)):raise ValueError('Owned watch paths required')
    state=json.loads((scheduler/'progress.json').read_text(encoding='utf-8'));pid=state['pid'];identity=process_identity(pid)
    result=dict(schema_version=1,status='watching',started_at_utc=datetime.now(timezone.utc).isoformat(),config=config,
        config_sha256=sha256(config_path),scheduler_pid=pid,scheduler_creation=identity,decisions=[])
    if identity is None:raise ValueError('Expected live owned scheduler at watcher launch')
    probe_sleep_permission();result['sleep_privilege_probe_passed']=True
    def write():
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'progress.json')
    def alive():return process_identity(pid)==identity
    def worker_stop(current):
        for row in reversed(current.get('stages',[])):
            args=row.get('command',[])
            if row.get('returncode') is not None or len(args)<2 or Path(args[1]).name!='train_dru_cfa.py' or '--output' not in args:continue
            folder=Path(args[args.index('--output')+1]);folder=folder if folder.is_absolute() else root/folder
            if not folder.resolve().is_relative_to(build.resolve()):raise ValueError('Unowned worker stop target')
            stop=folder.parent/'STOP';stop.write_text('User-authorized study stop before sleep.\n',encoding='utf-8')
            return row.get('pid')
        return None
    def stop_owned():
        current=json.loads((scheduler/'progress.json').read_text(encoding='utf-8'))
        child=worker_stop(current);child_identity=process_identity(child) if child else None
        end=time.monotonic()+90
        while alive() and time.monotonic()<end:
            if child is None or process_identity(child)!=child_identity:
                (scheduler/'STOP').write_text('Study watcher requests scheduler stop.\n',encoding='utf-8')
            time.sleep(2)
        if alive():
            (scheduler/'STOP').write_text('Study watcher hard-stop fallback.\n',encoding='utf-8');time.sleep(35)
        if alive():
            subprocess.run(['taskkill','/PID',str(pid),'/T','/F'],capture_output=True,timeout=20,check=False)
            time.sleep(5)
        if alive():raise RuntimeError('Owned scheduler did not terminate; sleep withheld')
        if child and child_identity is not None and process_identity(child)==child_identity:
            subprocess.run(['taskkill','/PID',str(child),'/T','/F'],capture_output=True,timeout=20,check=False);time.sleep(5)
            if process_identity(child)==child_identity:raise RuntimeError('Owned worker still alive; sleep withheld')
    write()
    try:
        while True:
            if (output/'CANCEL_SLEEP').exists():result['status']='cancelled_sleep_watch';break
            if time.time()>=datetime.fromisoformat(config['watch_end_utc']).timestamp():
                result['status']='watch_deadline_reached_no_late_sleep';break
            state=json.loads((scheduler/'progress.json').read_text(encoding='utf-8'))
            promising=evidence(state,comparison);choice=decision(alive(),time.time(),deadline,bool(promising))
            row=dict(at_utc=datetime.now(timezone.utc).isoformat(),choice=choice,scheduler_status=state['status'],promising=promising)
            # Bounded log: record transitions only, not a transcript of every idle poll.
            if not result['decisions'] or (choice,state['status'])!=(result['decisions'][-1]['choice'],result['decisions'][-1]['scheduler_status']):
                result['decisions'].append(row)
            result['status']=choice;result['last_check_utc']=row['at_utc'];write()
            if choice=='stop_and_sleep':
                stop_owned()
                while input_idle_seconds()<config['minimum_idle_seconds'] or (output/'BLOCK_SLEEP').exists():
                    if (output/'CANCEL_SLEEP').exists():result['status']='cancelled_sleep_watch';return
                    if time.time()>=datetime.fromisoformat(config['watch_end_utc']).timestamp():
                        result['status']='sleep_deferred_user_active';return
                    result['status']='waiting_for_idle_or_peer';write();time.sleep(30)
                if (output/'CANCEL_SLEEP').exists():result['status']='cancelled_sleep_watch';break
                result['status']='sleep_requested';write()
                stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
                with (root/'docs/validation/raw'/f'neural-study-sleep-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
                    json.dump(result,stream,indent=2,allow_nan=False)
                suspend();result['status']='returned_from_sleep';break
            time.sleep(30)
    except Exception as error:
        result.update(status='watch_failed_sleep_not_assumed',error=str(error));raise
    finally:
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--config',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.config,a.output)
