"""Seal a single data intervention and open its reserve only after final training."""
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
from run_dru_objective_study import write


def prepare(root):
    import torch
    from dru_composite_controls import reserve,response
    from dru_composite_data import reserved_signal
    from joint_bayer_unet import sample_rgb
    from synthetic_bayer_data import PATTERNS
    build=root/'build-patch_neural_restoration';folder=build/'dru-composite-plan-v001';folder.mkdir(exist_ok=False)
    prior_path=build/'dru-haar-convergence-plan-v001/haar.json';prior=json.loads(prior_path.read_text(encoding='utf-8'))
    held=reserve();conditions=[]
    for case in held['cases']:
        truth,wave,cosine,unit=reserved_signal(case)
        for phase in PATTERNS:
            clean=sample_rgb(truth,torch.tensor([phase]))
            fit=response(clean,clean,wave,cosine,unit,case['background']['kind']!='flat')
            if any(abs(r['sine_gain']-1)>1e-6 for r in fit):raise ValueError('Prospective fit not calibrated')
            conditions.extend(r['design_condition'] for r in fit)
    held['maximum_clean_fit_condition']=max(conditions)
    reserve_path=folder/'reserve.json';write(reserve_path,held)
    now=datetime.now(timezone.utc);hard=(now+timedelta(hours=4)).isoformat();training=(now+timedelta(hours=3,minutes=45)).isoformat()
    tools=set(prior['tools'])|{'dru_composite_data.py','dru_composite_controls.py','evaluate_dru_composites.py','run_dru_composite_study.py'}
    hashes={n:sha256(root/'tests/neural'/n) for n in sorted(tools)}
    changed=[s for s in range(4096) if s%8<2 and ((s//8)*2+s%8)%3!=0]
    manifest=dict(schema_version=1,status='sealed_composite_intervention',created_at_utc=now.isoformat(),tools=hashes,
        hard_stop_utc=hard,reserve=dict(path=str(reserve_path),sha256=sha256(reserve_path)),baselines=[],plans=[],
        fixed=dict(architecture_parameters=32643840,seeds=[71,73],data_seed=71,steps=1024,weight=.1,learning_rate=.0001),
        intervention=dict(changed_serials=changed,unchanged_exposures=4096-len(changed),
            description='Only add achromatic edge or coarse wave in two of three existing texture slots. Original weak signal, amplitudes, frequencies and all other families are unchanged.'),
        acceptance='Report all groups/seeds. An initial positive result requires both seeds to reduce reserved median absolute weak-gain error without increasing median MSE. This is not full validation.',
        limits=['Prospective synthetic reserve for this single intervention, not a photographic or all-project holdout.',
            'Frozen current Haar baseline already has 1024 updates; do not retrain or select it using reserve scores.',
            'Same Gaussian seeds and noise laws; heteroscedastic noise amplitudes change when the clean signal changes.'])
    if len(changed)!=682:raise ValueError('Intervention exposure count changed')
    for seed in (71,73):
        result_path=build/f'dru-haar-convergence-v001/haar/seed{seed}/result.json'
        state=json.loads(result_path.read_text(encoding='utf-8'))
        if state['status']!='completed_dru_bootstrap_not_photographic_qualification' or state['completed_steps']!=1024:raise ValueError('Complete frozen baseline required')
        final=next(r for r in state['milestones'] if r['step']==1024 and Path(r['path']).name.startswith('checkpoint-'))
        if sha256(Path(final['path']))!=final['sha256']:raise ValueError('Frozen baseline checkpoint changed')
        manifest['baselines'].append(dict(seed=seed,path=final['path'],sha256=final['sha256'],result=str(result_path),result_sha256=sha256(result_path)))
    for label,data_arm in [('baseline','reference'),('composite','composite')]:
        plan=dict(prior,data_arm=data_arm,tools=hashes,created_at_utc=now.isoformat(),training_deadline_utc=training,campaign_deadline_utc=hard,
            purpose='Single-factor composite data study, fixed Haar objective and 1024 updates',
            resources=dict(soft_mib=5120,hard_mib=6144,system_fraction=.92),
            load=dict(yield_memory_fraction=.85,yield_external_cpu_fraction=.8,pressure_seconds=15))
        path=folder/f'{label}.json';write(path,plan);manifest['plans'].append(dict(name=label,path=str(path),sha256=sha256(path),plan=plan))
    write(folder/'manifest.json',manifest)
    stamp=now.strftime('%Y%m%dT%H%M%SZ')
    with (root/'docs/validation/raw'/f'neural-dru-composite-plan-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
        json.dump(dict(manifest=manifest,reserve=held),stream,indent=2,allow_nan=False)
    print(folder/'manifest.json',flush=True)


def archive_known(root,campaign,manifest):
    memory=dict(observations=[],controls=[],units='Balanced camera site-response fits; gain dimensionless, MSE camera squared.')
    folders=[('baseline',r['seed'],Path(r['result']).parent) for r in manifest['baselines']]
    folders.extend(('composite',seed,campaign/'composite'/f'seed{seed}') for seed in (71,73))
    for arm,seed,folder in folders:
        for step in (0,128,256,512,1024):
            path=folder/f'control-{step:06d}.json'
            if not path.exists():continue
            control=json.loads(path.read_text(encoding='utf-8'));common=[{k:v for k,v in r.items() if k!='corrected'} for r in control['records']]
            if not memory['observations']:memory['observations']=common
            if len(common)!=864 or memory['observations']!=common:raise ValueError('Known controls are not paired')
            memory['controls'].append(dict(arm=arm,seed=seed,step=step,source_sha256=sha256(path),groups=control['groups'],corrected=[r['corrected'] for r in control['records']]))
    return memory


def run(root,plan_path):
    build=root/'build-patch_neural_restoration';output=build/'dru-composite-study-v001'
    if not plan_path.resolve().is_relative_to(build.resolve()):raise ValueError('Owned plan required')
    manifest=json.loads(plan_path.read_text(encoding='utf-8'))
    if manifest['status']!='sealed_composite_intervention':raise ValueError('Sealed experiment required')
    output.mkdir(exist_ok=False);deadline=datetime.fromisoformat(manifest['hard_stop_utc']).timestamp()
    gpu=root.parent/'venv-neural-cuda/Scripts/python.exe'
    result=dict(status='started',pid=os.getpid(),started_at_utc=datetime.now(timezone.utc).isoformat(),manifest_sha256=sha256(plan_path),stages=[])
    power=ctypes.WinDLL('kernel32',use_last_error=True)
    power.SetThreadExecutionState.argtypes=[ctypes.c_uint];power.SetThreadExecutionState.restype=ctypes.c_uint
    if not power.SetThreadExecutionState(0x80000001):raise ctypes.WinError(ctypes.get_last_error())
    def report():write(output/'progress.json',result)
    def check():
        if time.time()>=deadline:raise TimeoutError('Four-hour composite experiment deadline')
        if (output/'STOP').exists():raise InterruptedError('User composite stop')
        if any(sha256(root/'tests/neural'/n)!=h for n,h in manifest['tools'].items()):raise ValueError('Frozen experiment source changed')
    def admit():
        stable=None;started=time.monotonic();result['status']='waiting_for_memory'
        while True:
            check();memory=snapshot();result['admission_memory']=memory;report()
            if time.monotonic()-started>1200:raise TimeoutError('Memory admission timeout')
            if memory['system_used_fraction']<.70:
                if stable is None:stable=time.monotonic()
                if time.monotonic()-stable>=60:return
            else:stable=None
            time.sleep(10)
    def command(name,args,stop_folder):
        admit();row=dict(name=name,command=[str(gpu),*map(str,args)])
        result['stages'].append(row);result['status']=name;report()
        with (output/f'{name}.log').open('x',encoding='utf-8') as log:
            child=subprocess.Popen(row['command'],cwd=root,stdout=log,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            row['pid']=child.pid;report()
            try:
                while True:
                    check()
                    try:code=child.wait(timeout=min(30.,max(.1,deadline-time.time())));break
                    except subprocess.TimeoutExpired:report()
            except BaseException:
                stop_folder.mkdir(exist_ok=True);(stop_folder/'STOP').write_text('Composite experiment stop; checkpoint first.\n',encoding='utf-8')
                try:child.wait(timeout=90)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],capture_output=True,timeout=20,check=False)
                    if child.poll() is None:child.kill()
                    child.wait(timeout=15)
                raise
        row.update(returncode=code,finished_at_utc=datetime.now(timezone.utc).isoformat());report()
        if code:raise RuntimeError('Experiment worker failed: '+name)
    def train(plan,seed,preflight):
        path=Path(plan['path'])
        if sha256(path)!=plan['sha256']:raise ValueError('Training plan changed')
        folder=output/(plan['name']+'-preflight') if preflight else output/'composite'/f'seed{seed}'
        folder.parent.mkdir(exist_ok=True)
        args=[root/'tests/neural/train_dru_cfa.py','--plan',path,'--output',folder,'--seed',seed]
        if preflight:args.append('--preflight')
        command(plan['name']+('-preflight' if preflight else '-'+str(seed)),args,folder.parent)
        state=json.loads((folder/'result.json').read_text(encoding='utf-8'))
        expected='passed_dru_training_preflight_discarded' if preflight else 'completed_dru_bootstrap_not_photographic_qualification'
        if state['status']!=expected or preflight and not state.get('resume_bit_exact'):raise ValueError('Worker incomplete or not reproducible')
        if plan['name']=='baseline':
            prior=json.loads((build/'dru-haar-convergence-v001/haar-preflight/result.json').read_text(encoding='utf-8'))
            if state['losses']!=prior['losses'] or state['initial_model_fingerprint']!=prior['initial_model_fingerprint']:raise ValueError('Unchanged baseline replay differs')
            result['baseline_eight_update_replay_exact']=True;report()
    try:
        for plan in manifest['plans']:train(plan,71,True)
        candidate=next(p for p in manifest['plans'] if p['name']=='composite')
        for seed in (71,73):train(candidate,seed,False)
        command('once-reserved-evaluation',[root/'tests/neural/evaluate_dru_composites.py','--manifest',plan_path,
            '--campaign',output,'--output',output/'reserved-evaluation'],output)
        evaluated=json.loads((output/'reserved-evaluation/result.json').read_text(encoding='utf-8'))
        if evaluated['status']!='completed_once_prospective_composite_evaluation':raise ValueError('Reserve evaluation incomplete')
        result['reserved_summary']=[dict(arm=m['arm'],seed=m['seed'],**m['assessment']) for m in evaluated['models']]
        result['joint_median_improvement_both_seeds']=evaluated['joint_median_improvement_both_seeds']
        result['status']='completed_composite_experiment_not_production_validation'
    except Exception as error:
        result.update(status='stopped_on_error_or_load',error=str(error));raise
    finally:
        power.SetThreadExecutionState(0x80000000)
        try:result['known_control_memory']=archive_known(root,output,manifest)
        except Exception as error:result['archive_error']=str(error)
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat();report()
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-composite-study-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--prepare',action='store_true');p.add_argument('--plan',type=Path)
    args=p.parse_args();root=Path(__file__).resolve().parents[2]
    if args.prepare:prepare(root)
    elif args.plan is not None:run(root,args.plan)
    else:p.error('--prepare or --plan required')
