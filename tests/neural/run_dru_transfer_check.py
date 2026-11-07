"""Sequential transfer probe and its analysis, with bounded subprocess lifetimes."""
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    folder=build/'dru-structure-transfer-retry-v001';folder.mkdir(exist_ok=False)
    source=build/'dru-structure-amplitude-retry-v001';analysis=build/'dru-structure-amplitude-analysis-v001'
    if source.exists() or analysis.exists():raise ValueError('Fresh retry folders required')
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';cpu=root/'.venv-neural/Scripts/python.exe'
    commands=[('probe',gpu,'probe_dru_amplitudes.py',['--campaign',str(build/'dru-objective-study-v001'),
        '--output',str(source),'--arms','reference','structured'],1320),
        ('analysis',cpu,'analyse_dru_amplitudes.py',['--source',str(source/'result.json'),'--output',str(analysis/'result.json')],180)]
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),stages=[],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('run_dru_transfer_check.py','probe_dru_amplitudes.py','analyse_dru_amplitudes.py')},
        scope='Retry of memory-limited synthetic control after dev image-layer merge; not native-pipeline qualification.')
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


if __name__=='__main__':run()
