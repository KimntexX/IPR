"""Known-data-only gate chain for curve checkpoints, no new reserve preparation."""
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import sys
import time
from campaign_artifacts import sha256


def development_passed(proof):
    rows=proof.get('models',[])
    return (proof.get('status')=='completed_fixed_edge_protection_probe_not_photographic_qualification'
        and proof.get('pilot_gates_passed') is True and len(rows)==2
        and {r.get('seed') for r in rows}=={71,73} and all(r.get('pilot_gates_passed') is True for r in rows))


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'curve-known-controls-v001';output.mkdir(exist_ok=False)
    plan=build/'curve-known-development-plan-v001/plan.json'
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        development_plan_sha256=sha256(plan),stages=[],regressions=[],
        policy='Both final curve seeds on exact known observations, unchanged criteria. Development -> fine -> classics; failure stops following stages. No new reserve or fitting.',
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('run_curve_controls.py','qualify_sensor_curve.py','prepare_neural_regression.py','confirm_fine_noise.py','confirm_noisy_model.py')})
    deadline=time.monotonic()+5400
    def write():
        temp=output/'progress.tmp';temp.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');temp.replace(output/'progress.json')
    def command(name,script,*arguments):
        if any(sha256(Path(__file__).with_name(n))!=h for n,h in result['tools'].items()):raise ValueError('Control pipeline changed')
        entry=dict(name=name,started_at_utc=datetime.now(timezone.utc).isoformat(),command=[sys.executable,str(root/'tests/neural'/script),*map(str,arguments)])
        result['stages'].append(entry);result['status']=name;write()
        with (output/(name+'.log')).open('x',encoding='utf-8') as log:
            child=subprocess.Popen(entry['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                while True:
                    remaining=deadline-time.monotonic()
                    if remaining<=0:raise TimeoutError('Known-controls90minute budget reached')
                    try:code=child.wait(timeout=min(60.,remaining));break
                    except subprocess.TimeoutExpired:write()
            except BaseException:
                try:subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,timeout=15,check=False)
                finally:
                    if child.poll() is None:child.kill()
                    child.wait(timeout=15)
                raise
        entry.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());write()
        if code:raise RuntimeError('Control failed technically: '+name)
    try:
        command('development','qualify_sensor_curve.py','--plan',plan)
        path=build/'curve-known-development-v001/result.json';proof=json.loads(path.read_text(encoding='utf-8'))
        result['development_result_sha256']=sha256(path);result['development_passed']=development_passed(proof)
        if not result['development_passed']:
            result['status']='completed_known_development_failed_regressions_not_started';return
        for name,kind,historical in (('fine','fine','fixed-point-fine-v001'),
                ('classic_first','classic','noisy-confirmation-attempt02-v001'),('classic_second','classic','fixed-point-independent-v001')):
            prepared=build/f'curve-known-{name}-plan-v001';destination=build/f'curve-known-{name}-v001'
            command('prepare-'+name,'prepare_neural_regression.py','--source',path,'--historical',build/historical/'result.json','--kind',kind,'--output',prepared)
            command(name,'confirm_fine_noise.py' if kind=='fine' else 'confirm_noisy_model.py','--plan',prepared/'plan.json','--output',destination)
            tested=json.loads((destination/'result.json').read_text(encoding='utf-8'))
            passed=tested.get('pilot_gates_passed') is True and tested['status'].startswith('completed_')
            result['regressions'].append(dict(name=name,passed=passed,result=str(destination/'result.json'),sha256=sha256(destination/'result.json')))
            if not passed:
                result['status']='completed_known_regression_failed_remaining_not_started';return
        result['status']='completed_known_controls_passed_not_photographic_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();write()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-curve-known-controls-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':run()
