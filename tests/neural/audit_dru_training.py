"""Read-only replay of all actual256-step exposures and known strong controls, no model fitting."""
from collections import Counter,defaultdict
from datetime import datetime,timezone
import json
import math
from pathlib import Path
import statistics
import time
import torch
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from dru_training_data import example
from dru_audit_math import recipe,loss_probe,sigma_at_signal
from confirm_fine_noise import fine_signal,observation
from synthetic_bayer_data import PATTERNS
from run_synthetic_joint_pilot import tensor_hash
from run_noisy_moment_pilot import NOISE


def summary(rows):
    return dict(exposures=len(rows),mean_losses={key:statistics.mean(r['probe']['losses'][key]['noise_normalized_mse'] for r in rows)
        for key in ('identity','0.25','0.5','1.0')})


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    output=build/'dru-training-audit-v001';output.mkdir(exist_ok=False)
    source=build/'dru-memory-plan-v001/plan.json';plan=json.loads(source.read_text(encoding='utf-8'))
    noise_path=Path(plan['noise_source']);noise=json.loads(noise_path.read_text(encoding='utf-8'))
    if sha256(noise_path)!=plan['noise_sha256']:raise ValueError('Training noise source changed')
    for name,digest in plan['tools'].items():
        if sha256(root/'tests/neural'/name)!=digest:raise ValueError('Measured training source changed: '+name)
    fine_path=build/'curve-known-fine-v001/result.json';fine=json.loads(fine_path.read_text(encoding='utf-8'))['plan']
    tools={n:sha256(Path(__file__).with_name(n)) for n in ('audit_dru_training.py','dru_audit_math.py','dru_training_data.py','sensor_curve_noise.py','context_data.py','fine_texture_training.py')}
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        source_plan_sha256=sha256(source),source_plan=plan,noise_sha256=sha256(noise_path),fine_source_sha256=sha256(fine_path),tools=tools,
        units='Packed camera units = balanced camera /2. Probe raw_mse means unnormalized packed-camera MSE; sigma metadata below converted back to balanced camera.',
        scope='All2048actual training exposures from two256step runs, all864known strong noisy controls, no weights loaded/trained, no new reserve.',
        training=[],controls=[],analytic_sigma_at_signal03=[dict(scene=s['name'],parameters=sigma_at_signal(s,.3)) for s in noise['scenes']],
        old_control_sigma_at_signal03=[dict(noise_index=i,a=a,b=b,sigma=math.sqrt(a*.3+b)) for i,(a,b) in enumerate(NOISE) if i],
        witnesses='Fixed isotropic3x3per-CFA-plane averages blended at.25/.5/1, decided before replay; diagnostics only, no proposed denoiser or threshold optimization.')
    started=time.monotonic();guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    def check():
        guard.check()
        if time.monotonic()-started>600:raise TimeoutError('Ten-minute CPU audit budget')
    try:
        with guard,torch.inference_mode():
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            for seed in plan['seeds']:
                for serial in range(1024):
                    check();row=recipe(serial,seed,noise['scenes']);row['seed']=seed
                    packed,sigma,tags,target=example(serial,seed,noise['scenes'])
                    core=target[:,:,192:320,192:320];sample=sigma[:,:,192:320:4,192:320:4]*2
                    row.update(target_core_sha256=tensor_hash(core),observed_core_sha256=tensor_hash(packed[:,:,192:320,192:320]),
                        sigma_core_sha256=tensor_hash(sigma[:,:,192:320,192:320]),
                        sigma_q05_q50_q95_by_canonical_plane=torch.quantile(sample.flatten(2),torch.tensor([.05,.5,.95]),dim=2).permute(1,2,0).tolist()[0],
                        clean_core_range_balanced=[float(core.min()*2),float(core.max()*2)],
                        clean_context_range_balanced=[float(target.min()*2),float(target.max()*2)],
                        clean_core_negative_fraction=float((core<0).float().mean()),
                        clean_context_negative_fraction=float((target<0).float().mean()),
                        clean_core_above_white_fraction=float((core>.5).float().mean()),
                        clean_context_above_white_fraction=float((target>.5).float().mean()),
                        probe=loss_probe(packed,target,sigma,192))
                    result['training'].append(row)
                    if (serial+1)%256==0:print('AUDIT TRAIN',seed,serial+1,'/1024',flush=True)
                    del packed,sigma,tags,target,core,sample
            fields=[r for r in fine['fields'] if fine['cases'][r['case_index']]['amplitude']==.06 and r['noise_index']>0]
            if len(fields)!=864:raise ValueError('All known strong noisy fields required')
            for field in fields:
                check();case=fine['cases'][field['case_index']];truth,_,_=fine_signal(case)
                if tensor_hash(truth)!=case['target_sha256']:raise ValueError('Known target changed')
                tags=torch.tensor([PATTERNS[field['phase_index']]])
                clean,observed,sigma=observation(truth,tags,field)
                if tensor_hash(observed)!=field['observation_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:raise ValueError('Known observation changed')
                result['controls'].append(dict(**field,case=case,probe=loss_probe(observed/2,clean/2,sigma/2,32)))
            nonzero=[r for r in result['training'] if r['scenario_index'] is not None]
            by_family=defaultdict(list)
            for row in nonzero:by_family[row['family']].append(row)
            result['training_summary']=summary(nonzero)
            result['family_summary']={k:summary(v) for k,v in by_family.items()}
            result['control_summary']=summary(result['controls'])
            result['joint_counts']=[dict(family=k[0],phase_index=k[1],scenario_index=k[2],count=v)
                for k,v in Counter((r['family'],r['phase_index'],r['scenario_index']) for r in result['training']).items()]
            result['status']='completed_read_only_training_coverage_and_objective_audit'
            if any(sha256(Path(__file__).with_name(n))!=digest for n,digest in tools.items()):raise ValueError('Audit source changed')
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        with (output/'result.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-training-audit-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':run()
