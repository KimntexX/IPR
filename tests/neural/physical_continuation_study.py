"""Paired short continuation: current L1 versus noisy-sigma-normalized L1.

Reuse immutable arrays and the full parent checkpoint. No new photos or production changes.
"""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
from prepare_next_training import digest,read,write

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build-patch_neural_restoration'


def prepare(output):
    parent=BUILD/'physical-natural-training-v005'
    source_plan=read(parent/'plan.json');progress=read(parent/'seed71/progress.json')
    data=read(parent/'data.json');checkpoint=parent/'seed71/resume.pt'
    if (progress['status']!='completed_natural_physical_development_training' or progress['completed_steps']!=100000
            or digest(checkpoint)!=progress['final_checkpoint_sha256'] or digest(parent/'plan.json')!=progress['plan_sha256']):
        raise ValueError('Completed verified100000-step parent required')
    for name,expected in data['files'].items():
        if digest(parent/name)!=expected:
            raise ValueError('Parent arrays changed: '+name)
    output.mkdir(exist_ok=False)
    arms=[]
    for name,objective in (('current','unweighted L1'),('normalized','sigma_normalized_l1')):
        target=output/name;target.mkdir()
        for filename in data['files']:
            os.link(parent/filename,target/filename)
        write(target/'data.json',data)
        tools={filename:digest(ROOT/'tests/neural'/filename) for filename in
               (*source_plan['tools'],'physical_training_objective.py')}
        plan={**source_plan,'schema_version':3,'status':'frozen_paired_continuation',
            'created_at_utc':datetime.now(timezone.utc).isoformat(),'steps':4096,'campaign_seconds':2400,
            'seeds':[71],'objective':objective,'learning_rate':1e-6,'minimum_learning_rate':1e-6,
            'schedule':'Constant1e-6 for both arms, continuing the final parent learning rate',
            'sigma_floor':1/source_plan['target_domain']['divisor'],'progress_every':128,
            'controls_at':[0,512,1024,2048,4096],'checkpoint_at':[2048,4096],'tools':tools,
            'continuation':dict(checkpoint=str(checkpoint.resolve()),checkpoint_sha256=digest(checkpoint),
                completed_steps=100000,plan_sha256=digest(parent/'plan.json'),progress=str((parent/'seed71/progress.json').resolve()),
                progress_sha256=digest(parent/'seed71/progress.json'),optimizer='Restore identical Adam moments and step counter',
                random_state='Restore identical data generator, CPU and CUDA RNG state in each arm'),
            'limits':source_plan['limits']+['Two continuations of one initialization, known development only',
                'No dataset, simulator, sampling, adapter, gradient threshold or optimizer reset between arms',
                'Sigma floor equals one native DN; sigma from noisy input, never from clean target',
                'Independent hardlinks to read-only arrays; parent arrays and checkpoint must stay immutable']}
        write(target/'plan.json',plan)
        arms.append(dict(name=name,path=str(target.resolve()),objective=objective,plan_sha256=digest(target/'plan.json')))
    freeze=dict(schema_version=1,data_version='v001',campaign_id=output.name,
        measured_at_utc=datetime.now(timezone.utc).isoformat(),status='prepared',arms=arms,
        parent_checkpoint_sha256=digest(checkpoint),steps_per_arm=4096,
        maximum_training_seconds=4800,resources='Sequential GPU workers; private5GiB soft/6GiB hard, CUDA50%, system92%; host pressure yields',
        tools={name:digest(ROOT/'tests/neural'/name) for name in ('physical_continuation_study.py','physical_training_objective.py')},
        evaluation='Same72 noisy observations, comparisons per source/ISO and paired arms; no training-data split change',
        no_automatic_sleep=True)
    write(output/'study.json',freeze)


def summarize(output,study):
    arms=[read(Path(arm['path'])/'seed71/progress.json') for arm in study['arms']]
    if any(r['completed_steps']!=4096 or r['status']!='completed_natural_physical_development_training' for r in arms):
        raise ValueError('Both arms must complete4096 updates')
    if arms[0]['initial_model_fingerprint']!=arms[1]['initial_model_fingerprint']:
        raise ValueError('Different initial weights between paired arms')
    if arms[0]['input_selection_trace_sha256']!=arms[1]['input_selection_trace_sha256']:
        raise ValueError('Different data selections between paired arms')
    key=lambda rows:{(r['target_index'],r['iso']):r for r in rows}
    current,normalized=[key(r['controls'][-1]['rows']) for r in arms]
    if current.keys()!=normalized.keys():
        raise ValueError('Different final control identities')
    rows=[]
    for identity in sorted(current):
        a,b=current[identity],normalized[identity]
        if abs(a['observed_mse']/b['observed_mse']-1)>1e-6:
            raise ValueError('Different observed noise in paired controls')
        rows.append(dict(target_index=identity[0],source=identity[0]//9,iso=identity[1],
            current=a,normalized=b,normalized_minus_current_db=b['psnr_gain_db']-a['psnr_gain_db']))
    groups=[]
    for iso in (3200,6400,16000,51200):
        selected=[r for r in rows if r['iso']==iso]
        by_source=[statistics.median(r['normalized_minus_current_db'] for r in selected if r['source']==s) for s in (0,1)]
        groups.append(dict(iso=iso,median_paired_tile_gain_db=statistics.median(r['normalized_minus_current_db'] for r in selected),
            by_source_db=by_source,median_sources_db=statistics.median(by_source)))
    return dict(status='completed_paired_continuation_development_not_model_validation',rows=rows,summary=groups,
        identical_start=True,identical_input_selection_trace=True,
        parent_reproduction=[r['parent_control_reproduction_max_relative_error'] for r in arms],
        limits=['One initialization, two known source images; no fresh test or photographic validation',
                'Low-rate4096-step experiment; absence of improvement does not exclude a longer adaptation',
                'Noise simulator retains its declared physical qualification limits'])


def run(output):
    study=read(output/'study.json')
    result=dict(schema_version=1,data_version='v001',campaign_id=output.name,
        measured_at_utc=datetime.now(timezone.utc).isoformat(),status='running',study=study,stages=[])
    env=dict(os.environ,OMP_NUM_THREADS='4',OPENBLAS_NUM_THREADS='1')
    python=ROOT.parent/'venv-neural-cuda/Scripts/python.exe'
    try:
        for arm in study['arms']:
            for name,expected in study['tools'].items():
                if digest(ROOT/'tests/neural'/name)!=expected:
                    raise ValueError('Frozen study source changed')
            if digest(Path(arm['path'])/'plan.json')!=arm['plan_sha256']:
                raise ValueError('Frozen arm plan changed')
            result['active_arm']=arm['name'];write(output/'progress.json',result)
            with (output/(arm['name']+'-supervisor.log')).open('x',encoding='utf-8') as log:
                process=subprocess.Popen([str(python),str(ROOT/'tests/neural/physical_natural_training.py'),
                    '--mode','run','--output',arm['path']],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,
                    creationflags=subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)
                try:
                    returncode=process.wait(timeout=3100)
                except BaseException:
                    (Path(arm['path'])/'STOP').write_text('Study cancelled: save and stop.',encoding='utf-8')
                    try:process.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True,timeout=20,check=False)
                        process.wait(timeout=15)
                    raise
            arm_result=read(Path(arm['path'])/'progress.json')
            result['stages'].append(dict(arm=arm['name'],returncode=returncode,status=arm_result['status']))
            if returncode or arm_result['status']!='completed_declared_seeds_development_only':
                raise RuntimeError('Continuation arm failed: '+arm['name'])
        result.update(summarize(output,study))
    except Exception as error:
        result.update(status='stopped',error=str(error))
    finally:
        result['finished_at_utc']=datetime.now(timezone.utc).isoformat()
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (ROOT/'docs/validation/raw'/f'neural-continuation-study-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--prepare',action='store_true')
    args=parser.parse_args()
    if args.prepare:prepare(args.output.resolve())
    else:run(args.output.resolve())
