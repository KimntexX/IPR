"""Sequential transfer probe and its analysis, with bounded subprocess lifetimes."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256


def run(campaign,folder,steps,arms):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    campaign=campaign.resolve();folder=folder.resolve()
    if not all(p.is_relative_to(build.resolve()) and p!=build.resolve() for p in (campaign,folder)):raise ValueError('Owned campaign paths required')
    folder.mkdir(exist_ok=False)
    source=folder/'probe';analysis=folder/'analysis'
    if source.exists() or analysis.exists():raise ValueError('Fresh retry folders required')
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';cpu=root/'.venv-neural/Scripts/python.exe'
    commands=[('probe',gpu,'probe_dru_amplitudes.py',['--campaign',str(campaign),
        '--output',str(source),'--arms',*arms,'--steps',*map(str,steps)],1320),
        ('analysis',cpu,'analyse_dru_amplitudes.py',['--source',str(source/'result.json'),'--output',str(analysis/'result.json')],180)]
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),stages=[],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('run_dru_transfer_check.py','probe_dru_amplitudes.py','analyse_dru_amplitudes.py')},
        scope='Declared amplitude controls on saved milestones; no training or native-pipeline qualification.')
    def write():
        temp=folder/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(folder/'progress.json')
    try:
        for name,python,script,args,timeout in commands:
            if any(sha256(Path(__file__).with_name(n))!=h for n,h in result['tools'].items()):raise ValueError('Transfer tool changed')
            row=dict(name=name,command=[str(python),str(Path(__file__).with_name(script)),*args],timeout_seconds=timeout)
            result['stages'].append(row);result['status']=name;write();started=time.monotonic()
            with (folder/f'{name}.log').open('x',encoding='utf-8') as log:
                # Workers do not spawn subprocesses; run() kills and reaps this owned worker on timeout.
                completed=subprocess.run(row['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=timeout,
                    creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS,check=False)
            row.update(returncode=completed.returncode,elapsed_seconds=time.monotonic()-started);write()
            if completed.returncode:raise RuntimeError('Transfer stage failed: '+name)
        result['status']='completed_probe_and_analysis'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-transfer-retry-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--steps',nargs='+',type=int,required=True);parser.add_argument('--arms',nargs='+',required=True)
    args=parser.parse_args();run(args.campaign,args.output,args.steps,args.arms)
