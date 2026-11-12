"""Open the sealed composite reserve after candidate checkpoints are final."""
import argparse
from datetime import datetime,timezone
import gc
import json
import os
from pathlib import Path
import statistics
import time
os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def assess(records,reserve):
    groups=[]
    for kind in ('flat','edge','coarse'):
        for chromatic in (False,True):
            for amplitude in (.006,.018):
                for band in (0,1):
                    for level in (1,2):
                        selected=[r for r in records if (c:=reserve['cases'][r['case_index']])['background']['kind']==kind
                            and c['chromatic']==chromatic and c['amplitude']==amplitude and c['frequency_bin']==band and r['noise_index']==level]
                        if len(selected)!=8:raise ValueError('All phases and replicates required per reserved stratum')
                        gains=[]
                        for colour in range(4):gains.append(statistics.mean(r['corrected'][r['site_colours'].index(colour)]['sine_gain'] for r in selected))
                        mse=sum(v['mse'] for r in selected for v in r['corrected']);observed=sum(v['mse'] for r in selected for v in r['observed'])
                        groups.append(dict(background=kind,chromatic=chromatic,amplitude=amplitude,frequency_bin=band,noise_index=level,
                            mean_gain_by_colour=gains,mse_ratio=mse/observed,mean_absolute_gain_error=statistics.mean(abs(v-1) for v in gains)))
    return dict(groups=groups,median_gain_error=statistics.median(r['mean_absolute_gain_error'] for r in groups),
        median_mse_ratio=statistics.median(r['mse_ratio'] for r in groups),
        minimum_gain=min(g for r in groups for g in r['mean_gain_by_colour']),maximum_mse_ratio=max(r['mse_ratio'] for r in groups))


def run(manifest_path,campaign,output):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (manifest_path,campaign,output)):raise ValueError('Owned evaluation paths required')
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'));path=Path(manifest['reserve']['path'])
    if sha256(path)!=manifest['reserve']['sha256']:raise ValueError('Prospective reserve changed')
    reserve=json.loads(path.read_text(encoding='utf-8'));models=[]
    for seed in (71,73):
        baseline=next(r for r in manifest['baselines'] if r['seed']==seed)
        old=json.loads(Path(baseline['result']).read_text(encoding='utf-8'))
        state_path=campaign/'composite'/f'seed{seed}/result.json';candidate=json.loads(state_path.read_text(encoding='utf-8'))
        if candidate['status']!='completed_dru_bootstrap_not_photographic_qualification' or candidate['completed_steps']!=1024:raise ValueError('Both candidates must be final before opening reserve')
        if candidate['initial_model_fingerprint']!=old['initial_model_fingerprint']:raise ValueError('Unpaired model initialization')
        final=next(r for r in candidate['milestones'] if r['step']==1024 and Path(r['path']).name.startswith('checkpoint-'))
        models.extend([dict(arm='baseline',seed=seed,path=baseline['path'],sha256=baseline['sha256']),
            dict(arm='composite',seed=seed,path=final['path'],sha256=final['sha256'])])
    output.mkdir(exist_ok=False);result=dict(schema_version=1,status='started',manifest_sha256=sha256(manifest_path),
        reserve=reserve,models=[],scope='One prospective synthetic holdout for this intervention, not a camera/photographic validation.')
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=5*1024**3,system_fraction=.92);started=time.monotonic()
    try:
        with guard:
            import torch
            from dru_cfa import create
            from dru_composite_controls import evaluate
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU unavailable or busy')
            torch.cuda.set_per_process_memory_fraction(.5)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            def check():
                guard.check()
                if (campaign/'STOP').exists():raise InterruptedError('User reserve stop')
                if time.monotonic()-started>1200:raise TimeoutError('20-minute reserve budget')
            for reference in models:
                check();path=Path(reference['path'])
                if not path.resolve().is_relative_to(build.resolve()) or sha256(path)!=reference['sha256']:raise ValueError('Model checkpoint changed')
                state=torch.load(path,weights_only=True,map_location='cpu');model,provenance=create(root,reference['seed'])
                model.load_state_dict(state['model_state'],strict=True);del state;gc.collect()
                model=model.cuda().eval().requires_grad_(False)
                records=evaluate(model,reserve,check)
                result['models'].append(dict(**reference,architecture=provenance,records=records,assessment=assess(records,reserve)))
                del model;gc.collect();torch.cuda.empty_cache()
                print('RESERVED',reference['arm'],reference['seed'],len(records),flush=True)
            by={(r['arm'],r['seed']):r['assessment'] for r in result['models']}
            result['joint_median_improvement_both_seeds']=all(by['composite',s]['median_gain_error']<by['baseline',s]['median_gain_error']
                and by['composite',s]['median_mse_ratio']<=by['baseline',s]['median_mse_ratio'] for s in (71,73))
            result['status']='completed_once_prospective_composite_evaluation'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        (output/'result.json').write_text(json.dumps(result,allow_nan=False,separators=(',',':')),encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-composite-reserve-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--campaign',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();run(args.manifest,args.campaign,args.output)
