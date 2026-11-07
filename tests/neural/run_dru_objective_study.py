"""Seal and run a paired objective experiment; all jobs bounded, sequential and owned."""
import argparse
import ctypes
from datetime import datetime,timezone,timedelta
import json
import os
from pathlib import Path
import subprocess
import time
from campaign_artifacts import sha256
from memory_budget import snapshot
from scheduled_comparison_results import metrics,promising,strict


def write(path,value):
    temporary=path.with_suffix('.tmp');temporary.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n',encoding='utf-8');temporary.replace(path)


def prepare(root):
    build=root/'build-patch_neural_restoration';folder=build/'dru-haar-convergence-plan-v001';folder.mkdir(exist_ok=False)
    source=build/'dru-data-comparison-plan-v001/reference.json';prior=json.loads(source.read_text(encoding='utf-8'))
    audit=json.loads((build/'dru-haar-recipe-audit-v001/result.json').read_text(encoding='utf-8'))
    if audit['status']!='completed_actual_recipe_haar_audit' or not all(audit['checks'].values()):raise ValueError('Complete successful recipe qualification required')
    for name,digest in audit['plan']['tools'].items():
        if sha256(root/'tests/neural'/name)!=digest:raise ValueError('Qualified Haar recipe source changed: '+name)
    now=datetime.now(timezone.utc);hard=(now+timedelta(hours=6)).isoformat();training=(now+timedelta(hours=5,minutes=45)).isoformat()
    names=set(prior['tools'])|{'dru_structure_objective.py','dru_haar_objective.py','dru_clean_variance.py','dru_execution.py','load_control.py','run_dru_objective_study.py','scheduled_comparison_results.py'}
    hashes={n:sha256(root/'tests/neural'/n) for n in sorted(names)}
    manifest=dict(schema_version=1,status='sealed_matched_objective_study',created_at_utc=now.isoformat(),
        source_sha256=sha256(source),tools=hashes,arms=[],hard_stop_utc=hard,output=str(build/'dru-haar-convergence-v001'),
        fixed=dict(seeds=[71,73],data_seed=71,data_arm='reference',steps=1024,learning_rate=.0001,
            accumulation=4,structure_weight=.1,haar_levels=3,coefficient_tile_side=8,energy_epsilon=1e-8),
        control_steps=[0,128,256,512,1024],
        audit=dict(path=str(build/'dru-haar-recipe-audit-v001/result.json'),sha256=sha256(build/'dru-haar-recipe-audit-v001/result.json'),
            reason='Training-recipe output-gradient ratio p95=9.25; fixed weight0.1 before any trained Haar scores. No validation-set weight tuning.'),
        policy='All four runs from scratch; no best seed/checkpoint, coefficient sweep, automatic continuation or production change.',
        limits=['Known development controls, not independent qualification.',
            'Noise-aware local Haar projection is an experimental constraint, not a published DRUNet objective.',
            'Increased residual noise or gradient artefacts remain failure modes.'])
    plans=[]
    for label,objective in [('reference','mse'),('haar','haar_noise_aware_v1')]:
        plan=dict(prior,tools=hashes,objective=objective,created_at_utc=now.isoformat(),training_deadline_utc=training,
            campaign_deadline_utc=hard,steps_per_seed=1024,milestones=[128,256,512,1024],structure_weight=.1,
            purpose='Matched 1024-update convergence comparison; MSE versus noise-aware local Haar, loss-only clean variance')
        path=folder/f'{label}.json';write(path,plan);plans.append(path)
        manifest['arms'].append(dict(name=label,path=str(path),sha256=sha256(path),plan=plan))
    execution=dict(schema_version=1,scientific_plan_sha256=[sha256(p) for p in plans],runtime_tools=hashes,tool_amendments={},
        training_deadline_utc=training,hard_stop_utc=hard,resources=dict(soft_mib=5120,hard_mib=6144,system_fraction=.92),
        load=dict(yield_memory_fraction=.85,yield_external_cpu_fraction=.8,pressure_seconds=15))
    write(folder/'execution.json',execution);manifest['execution']=dict(path=str(folder/'execution.json'),sha256=sha256(folder/'execution.json'))
    write(folder/'manifest.json',manifest)
    stamp=now.strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-objective-plan-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(dict(manifest=manifest,execution=execution),stream,indent=2,allow_nan=False)
    print(folder/'manifest.json',flush=True)


def summarize(output):
    result=dict(status='completed_matched_objective_study_known_only',rows=[],sources=[],paired_effects=[])
    controls={};states={};identities=None
    for arm in ('reference','haar'):
        for seed in (71,73):
            folder=output/arm/f'seed{seed}';path=folder/'result.json';state=json.loads(path.read_text(encoding='utf-8'))
            if state.get('status')!='completed_dru_bootstrap_not_photographic_qualification' or state.get('completed_steps')!=1024:
                raise ValueError('All four complete 1024-update runs required')
            states[arm,seed]=state;result['sources'].append(dict(path=str(path),sha256=sha256(path)))
            for step in (0,128,256,512,1024):
                path=folder/f'control-{step:06d}.json';value=json.loads(path.read_text(encoding='utf-8'))
                ids=[(r['observation_sha256'],r['sigma_sha256']) for r in value['records']]
                if len(ids)!=864 or len(set(ids))!=864 or identities is not None and ids!=identities:raise ValueError('Unpaired full controls')
                identities=ids;controls[arm,seed,step]=value
                result['sources'].append(dict(path=str(path),sha256=sha256(path)))
                result['rows'].append(dict(arm=arm,seed=seed,step=step,**metrics(value),groups=value['groups']))
    for seed in (71,73):
        if states['reference',seed]['initial_model_fingerprint']!=states['haar',seed]['initial_model_fingerprint']:
            raise ValueError('Initial model pairing failed')
        if controls['reference',seed,0]!=controls['haar',seed,0]:raise ValueError('Initial controls differ')
        for step in (128,256,512,1024):
            ref=controls['reference',seed,step]['groups'];candidate=controls['haar',seed,step]['groups']
            if len(ref)!=len(candidate):raise ValueError('Incomplete groups')
            for a,b in zip(ref,candidate):
                names=('frequency','chromatic','noise_index','fields')
                if any(a[k]!=b[k] for k in names):raise ValueError('Different groups')
                result['paired_effects'].append(dict(seed=seed,step=step,**{k:a[k] for k in names},
                    gain_delta=[y-x for x,y in zip(a['mean_sensor_gain_by_colour'],b['mean_sensor_gain_by_colour'])],
                    mse_ratio_delta=b['sensor_mse_ratio']-a['sensor_mse_ratio']))
    by={(r['arm'],r['seed']):r for r in result['rows'] if r['step']==1024}
    result['strict_both_seeds']=all(strict(by['haar',s]) for s in (71,73))
    result['promising_both_seeds']=all(promising(by['haar',s],by['reference',s]) for s in (71,73))
    write(output/'assessment.json',result)
    return result


def run(root,plan_path):
    build=root/'build-patch_neural_restoration'
    if not plan_path.resolve().is_relative_to(build.resolve()):raise ValueError('Owned plan required')
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if plan['status']!='sealed_matched_objective_study':raise ValueError('Sealed objective study required')
    output=Path(plan['output'])
    if not output.resolve().is_relative_to(build.resolve()) or output.resolve()==build.resolve():raise ValueError('Owned fresh campaign output required')
    output.mkdir(exist_ok=False);deadline=datetime.fromisoformat(plan['hard_stop_utc']).timestamp()
    result=dict(status='started',pid=os.getpid(),started_at_utc=datetime.now(timezone.utc).isoformat(),plan_sha256=sha256(plan_path),stages=[])
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe';execution=Path(plan['execution']['path'])
    if sha256(execution)!=plan['execution']['sha256']:raise ValueError('Execution contract changed')
    power=ctypes.WinDLL('kernel32',use_last_error=True)
    power.SetThreadExecutionState.argtypes=[ctypes.c_uint];power.SetThreadExecutionState.restype=ctypes.c_uint
    if not power.SetThreadExecutionState(0x80000001):raise ctypes.WinError(ctypes.get_last_error())
    def report():write(output/'progress.json',result)
    def check():
        if time.time()>=deadline:raise TimeoutError('Six-hour study deadline')
        if (output/'STOP').exists():raise InterruptedError('User study stop')
        if any(sha256(root/'tests/neural'/n)!=h for n,h in plan['tools'].items()):raise ValueError('Sealed source changed')
    def admit():
        stable=None;started=time.monotonic();result['status']='waiting_for_memory'
        while True:
            check();sample=snapshot();result['admission_memory']=sample;report()
            if time.monotonic()-started>1200:raise TimeoutError('20-minute admission limit')
            if sample['system_used_fraction']<.70:
                if stable is None:stable=time.monotonic()
                if time.monotonic()-stable>=60:return
            else:stable=None
            time.sleep(10)
    def command(arm,seed,preflight):
        admit();label=arm['name'];path=Path(arm['path'])
        if sha256(path)!=arm['sha256']:raise ValueError('Arm plan changed')
        folder=output/(label+'-preflight') if preflight else output/label/f'seed{seed}'
        folder.parent.mkdir(exist_ok=True)
        args=[str(gpu),str(root/'tests/neural/train_dru_cfa.py'),'--plan',str(path),'--output',str(folder),'--seed',str(seed),'--execution',str(execution)]
        if preflight:args.append('--preflight')
        name=f'{label}-'+('preflight' if preflight else str(seed));row=dict(name=name,command=args,output=str(folder))
        result['stages'].append(row);result['status']=name;report()
        with (output/f'{name}.log').open('x',encoding='utf-8') as log:
            child=subprocess.Popen(args,cwd=root,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            row['pid']=child.pid;report()
            try:
                while True:
                    check()
                    try:code=child.wait(timeout=min(30,max(.1,deadline-time.time())));break
                    except subprocess.TimeoutExpired:report()
            except BaseException:
                (folder.parent/'STOP').write_text('Objective study stop; preserve completed update.\n',encoding='utf-8')
                try:child.wait(timeout=90)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],capture_output=True,timeout=20,check=False)
                    if child.poll() is None:child.kill()
                    child.wait(timeout=15)
                raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());report()
        state=json.loads((folder/'result.json').read_text(encoding='utf-8'))
        expected='passed_dru_training_preflight_discarded' if preflight else 'completed_dru_bootstrap_not_photographic_qualification'
        if code or state['status']!=expected or preflight and not state.get('resume_bit_exact'):
            raise RuntimeError('Worker failed or paused; no automatic retries: '+name)
        if label=='reference':
            prior=json.loads((build/f'dru-data-comparison-v001/reference/seed{seed}/result.json').read_text(encoding='utf-8'))
            limit=8 if preflight else 256
            if state['losses'][:limit]!=prior['losses'][:limit]:raise ValueError('MSE replay losses changed')
            if not preflight:
                for step in (0,128,256):
                    old=build/f'dru-data-comparison-v001/reference/seed{seed}/control-{step:06d}.json'
                    if json.loads((folder/f'control-{step:06d}.json').read_text(encoding='utf-8'))!=json.loads(old.read_text(encoding='utf-8')):
                        raise ValueError('Baseline control replay differs')
            row['baseline_replay_verified']=True;report()
    try:
        for arm in plan['arms']:command(arm,71,True)
        for arm in plan['arms']:
            for seed in (71,73):command(arm,seed,False)
        result['assessment']=summarize(output);result['status']='completed_matched_objective_study'
    except Exception as error:
        result.update(status='stopped_on_error_or_load',error=str(error));raise
    finally:
        power.SetThreadExecutionState(0x80000000)
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();report()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-objective-study-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare',action='store_true');parser.add_argument('--plan',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2]
    if args.prepare:prepare(root)
    elif args.plan is not None:run(root,args.plan)
    else:parser.error('--prepare or --plan required')
