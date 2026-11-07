"""Bounded no-training amplitude sweep, all arms/seeds at both trained milestones."""
import argparse
from datetime import datetime, timezone
import gc
import json
import os
from pathlib import Path
import time

os.environ.setdefault('CUBLAS_WORKSPACE_CONFIG',':4096:8')
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def run(campaign=None,output=None,arms=('reference','noise','details')):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    campaign=build/'dru-data-comparison-v001' if campaign is None else campaign.resolve()
    output=build/'dru-amplitude-diagnosis-v001' if output is None else output.resolve()
    if not all(p.resolve().is_relative_to(build.resolve()) for p in (campaign,output)):raise ValueError('Owned diagnostic paths required')
    if len(set(arms))!=len(arms) or not arms or any(a not in ('reference','noise','details','structured') for a in arms):raise ValueError('Distinct declared arms required')
    output.mkdir(exist_ok=False)
    source=build/'curve-known-fine-v001/result.json'
    source_plan=json.loads(source.read_text(encoding='utf-8'))['plan']
    fields=[f for f in source_plan['fields'] if source_plan['cases'][f['case_index']]['amplitude']==.06
        and source_plan['cases'][f['case_index']]['frequency'] in (6,14,26)
        and f['phase_index']==0 and f['replicate']==0 and f['noise_index']>0]
    if len(fields)!=36:raise ValueError('Expected 36 balanced diagnostic fields')
    plan=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),amplitudes=[.008,.03,.06],
        frequencies=[6,14,26],angles=[.17,.63,1.07],milestones=[128,256],seeds=[71,73],
        arms=list(arms),campaign=str(campaign),base_fields=fields,source_sha256=sha256(source),
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('probe_dru_amplitudes.py','dru_cfa.py','confirm_fine_noise.py',
            'context_data.py','bayer_canonical.py','sensor_transfer_math.py')},models=[],
        limits=['Post-hoc known-development diagnostic, no independent validation or photographic claim.',
            'Single CFA phase and noise replicate, all three existing angles and both chromatic states/noise levels.',
            'Identical Gaussian noise realizations across amplitudes; all arms and both seeds retained.',
            'No optimizer, no training, no model selection, no threshold adjustment.',
            '128-to-256 trajectory cannot prove asymptotic convergence.'])
    for arm in plan['arms']:
        for seed in plan['seeds']:
            state_path=campaign/arm/f'seed{seed}/result.json'
            state=json.loads(state_path.read_text(encoding='utf-8'))
            if state['status']!='completed_dru_bootstrap_not_photographic_qualification' or state['completed_steps']!=256:
                raise ValueError('Complete parent runs required')
            for step in plan['milestones']:
                entries=[r for r in state['milestones'] if r['step']==step and Path(r['path']).name.startswith('checkpoint-')]
                if len(entries)!=1:raise ValueError('Unique milestone required')
                plan['models'].append(dict(arm=arm,seed=seed,step=step,**{k:entries[0][k] for k in ('path','sha256')},
                    parent_result_sha256=sha256(state_path)))
    (output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    result=dict(schema_version=1,status='started',plan=plan,records=[])
    guard=MemoryGuard(hard_bytes=6*1024**3,soft_bytes=5*1024**3,system_fraction=.92);started=time.monotonic()
    try:
        with guard:
            import torch
            from torch.nn import functional as F
            from dru_cfa import create
            from confirm_fine_noise import fine_signal,observation
            from context_data import extend_observation
            from bayer_canonical import canonicalize,spatial_reflect
            from run_noisy_moment_pilot import NOISE
            from run_synthetic_joint_pilot import tensor_hash
            from synthetic_bayer_data import PATTERNS
            from sensor_transfer_math import site_response
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            if not torch.cuda.is_available() or torch.cuda.mem_get_info()[0]<4*1024**3:raise RuntimeError('GPU absent or busy')
            torch.cuda.set_per_process_memory_fraction(.5)
            torch.backends.cuda.matmul.allow_tf32=False;torch.backends.cudnn.allow_tf32=False
            torch.backends.cudnn.benchmark=False;torch.backends.cudnn.deterministic=True
            def check():
                guard.check()
                if (output/'STOP').exists():raise RuntimeError('User requested diagnostic stop')
                if time.monotonic()-started>1200:raise TimeoutError('20-minute diagnostic budget')
            with torch.inference_mode():
                for reference in plan['models']:
                    check();path=Path(reference['path'])
                    if sha256(path)!=reference['sha256']:raise ValueError('Checkpoint changed')
                    state=torch.load(path,weights_only=True,map_location='cpu')
                    model,_=create(root,reference['seed']);model.load_state_dict(state['model_state'],strict=True)
                    del state;gc.collect();model=model.cuda().eval().requires_grad_(False)
                    for amplitude in plan['amplitudes']:
                        for field in fields:
                            check();case=dict(source_plan['cases'][field['case_index']],amplitude=amplitude)
                            truth,wave,cosine=fine_signal(case);tags=torch.tensor([PATTERNS[field['phase_index']]])
                            clean,observed,sigma=observation(truth,tags,field)
                            observed_sha=tensor_hash(observed);sigma_sha=tensor_hash(sigma)
                            expanded,_,_=fine_signal(case,1024,-416)
                            values,deviation=extend_observation(expanded,tags,observed,sigma,*NOISE[field['noise_index']],210000000+field['noise_seed'])
                            packed,deviations,labels,flags=canonicalize(values/2,deviation/2,tags)
                            corrected=model.correct_observations(packed.cuda(),deviations.cuda(),labels.cuda()).cpu()
                            restored=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(corrected,2),flags)*2,2)[:,:,208:304,208:304]
                            result['records'].append(dict(arm=reference['arm'],seed=reference['seed'],step=reference['step'],
                                case_index=field['case_index'],frequency=case['frequency'],amplitude=amplitude,angle=case['angle'],
                                chromatic=case['chromatic'],noise_index=field['noise_index'],noise_seed=field['noise_seed'],
                                site_colours=tags[0].tolist(),truth_sha256=tensor_hash(truth),observation_sha256=observed_sha,sigma_sha256=sigma_sha,
                                corrected=site_response(restored,clean,wave,cosine),observed=site_response(observed,clean,wave,cosine)))
                    del model;gc.collect();torch.cuda.empty_cache()
                    print('MODEL',reference['arm'],reference['seed'],reference['step'],'records',len(result['records']),flush=True)
                    (output/'progress.json').write_text(json.dumps(dict(status='running',records=len(result['records']),last_model=reference)),encoding='utf-8')
            result['status']='completed_amplitude_diagnostic_not_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        (output/'result.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-dru-amplitudes-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--campaign',type=Path);parser.add_argument('--output',type=Path)
    parser.add_argument('--arms',nargs='+',default=['reference','noise','details'])
    args=parser.parse_args();run(args.campaign,args.output,args.arms)
