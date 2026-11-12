"""Known-field correction-strength tradeoff, not a newly calibrated slider."""
import argparse
from datetime import datetime,timezone
import json
import math
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract,response
from bayer_canonical import canonicalize,spatial_reflect
from closed_moment_operator import interpolate_closed
from confirm_fine_noise import fine_signal,observation as fine_observation
from context_data import extend_case,extend_observation,crop_centre
from diagnose_train_learnability import digest,save
from edge_protected_noise import EdgeProtectedNoisyUNet
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from run_noisy_moment_pilot import NOISE,observation
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS
from trace_noise_reconstruction import coefficients


SCALES=tuple(i/8 for i in range(9))


def blend(values,corrected,scale):
    if isinstance(scale,bool) or not isinstance(scale,(int,float)) or not math.isfinite(scale) or not 0<=scale<=1:
        raise ValueError('Finite scale0..1 required')
    if corrected.shape!=values.shape:
        raise ValueError('Matching correction fields required')
    if scale==1:
        return corrected
    if scale==0:
        return values
    return values+scale*(corrected-values)


def cfa_measure(corrected,clean,observed,core):
    error=crop_centre(corrected-clean,core).double()
    raw=crop_centre(observed-clean,core).double()
    return dict(mse=float(error.square().mean()),mse_over_noisy=float(error.square().mean()/raw.square().mean()),
        spatial_variance=float(error.var(dim=(-2,-1),unbiased=False).mean()),
        bias_per_plane=error.mean(dim=(0,2,3)).tolist())


def run(root,output):
    base=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()):
        raise ValueError('Dedicated neural output required')
    sources={}
    histories={}
    for arm in ('control','stratified'):
        proof=base/f'coverage-{arm}-protected-v001/result.json'
        sources[arm]=source_contract(root,json.loads(proof.read_text(encoding='utf-8')))
        history_path=base/f'coverage-{arm}-fine-v001/result.json'
        histories[arm]=json.loads(history_path.read_text(encoding='utf-8'))
        check_inference(sources[arm]['data'],sources[arm]['tools'])
        if histories[arm]['status']!='completed_fine_signal_regression_not_photographic_qualification':
            raise ValueError('Complete known fine results required')
    now=datetime.now(timezone.utc)
    identity='neural-correction-tradeoff-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,campaign_id=identity,measured_at_utc=now.isoformat(),status='incomplete',
        script_sha256=digest(Path(__file__)),control_scales=SCALES,candidate_scales=[1.],records=[],flat_records=[],
        source_proof_sha256={arm:digest(base/f'coverage-{arm}-protected-v001/result.json') for arm in sources},
        selection='All consulted fine amplitudes/frequencies/colours, phase0 draw0, both positive levels/seeds; all four known constant mixed cases, same phase/levels.',
        limits=['Descriptive fixed grid, no fitted scale, no new training or reserved data. Candidate remains at full correction.',
            'CFA MSE includes bias and variance. Spatial variance on constants is descriptive, not independent pixel noise or a calibrated camera estimate.',
            'Single draw and phase; do not generalize exact noise matching or tune gates from this study.'])
    output.mkdir(parents=True,exist_ok=False)
    guard,started=MemoryGuard(**sources['control']['data']['memory_policy']),time.monotonic()
    try:
        with guard,torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for arm,source in sources.items():
                history=histories[arm]
                lookup={(r['seed'],r['case_index'],r['noise_index']):r for r in history['records'] if r['phase_index']==0 and r['replicate']==0}
                scales=SCALES if arm=='control' else (1.,)
                for ref in source['models']:
                    weights=Path(ref['folder'])/'weights.pt'
                    if digest(weights)!=ref['weights_sha256']:
                        raise ValueError('Final weights changed')
                    model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                    model.load_state_dict(torch.load(weights,weights_only=True)['state_dict'],strict=True)
                    for field in history['plan']['fields']:
                        if field['phase_index']!=0 or field['replicate']!=0 or field['noise_index']==0:
                            continue
                        guard.check()
                        case=history['plan']['cases'][field['case_index']]
                        truth,wave,quadrature=fine_signal(case)
                        expanded,_,_=fine_signal(case,384,-96)
                        if tensor_hash(truth)!=case['target_sha256']:
                            raise ValueError('Fine target changed')
                        tags=torch.tensor([PATTERNS[0]])
                        _,packed,sigma=fine_observation(truth,tags,field)
                        if tensor_hash(packed)!=field['observation_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:
                            raise ValueError('Fine observation changed')
                        packed,sigma=extend_observation(expanded,tags,packed,sigma,*NOISE[field['noise_index']],210000000+field['noise_seed'])
                        values,sigma,labels,flags=canonicalize(packed/2,sigma/2,tags)
                        clean,_,_,_=canonicalize(sample_rgb(expanded,tags)/2,torch.zeros_like(sigma),tags)
                        corrected=model.correct_observations(values,sigma,labels)
                        variants=[]
                        for scale in scales:
                            current=blend(values,corrected,scale)
                            image=interpolate_closed(current,labels,coefficients(model,current,labels))
                            measures=response(crop_centre(spatial_reflect(image,flags)*2,192),truth,wave,quadrature)
                            if scale in (0.,1.):
                                expected=lookup[ref['seed'],field['case_index'],field['noise_index']]['parent' if scale==0. else 'full']
                                if max(abs(a-b) for a,b in zip(measures['sine_gain'],expected['sine_gain']))>2e-6:
                                    raise ValueError('Endpoint differs from archived measurement')
                            variants.append(dict(scale=scale,rgb=measures,cfa=cfa_measure(current,clean,values,32)))
                        result['records'].append(dict(arm=arm,seed=ref['seed'],case=case,field=field,variants=variants))
                    data=source['data']
                    observations={(r['case_index'],r['noise_index']):r for r in data['observations'] if r['phase']==0}
                    for index,case in enumerate(data['mixed']):
                        if case['family']!='constant':
                            continue
                        expanded=extend_case(case,320)
                        truth=crop_centre(expanded)
                        if tensor_hash(truth)!=case['target_sha256']:
                            raise ValueError('Constant target changed')
                        tags=torch.tensor([PATTERNS[0]])
                        for level in (1,2):
                            guard.check()
                            field=observations[index,level]
                            packed,sigma=observation(truth,tags,level,field['noise_seed'])
                            if tensor_hash(packed)!=field['noisy_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:
                                raise ValueError('Constant observation changed')
                            packed,sigma=extend_observation(expanded,tags,packed,sigma,*NOISE[level],data['context_policy']['validation_halo_seed_base']+field['noise_seed'])
                            values,sigma,labels,_=canonicalize(packed/2,sigma/2,tags)
                            clean,_,_,_=canonicalize(sample_rgb(expanded,tags)/2,torch.zeros_like(sigma),tags)
                            corrected=model.correct_observations(values,sigma,labels)
                            result['flat_records'].append(dict(arm=arm,seed=ref['seed'],case=case,noise_index=level,
                                variants=[dict(scale=s,cfa=cfa_measure(blend(values,corrected,s),clean,values,16)) for s in scales]))
                    print('TRADEOFF',arm,ref['seed'],flush=True)
            if len(result['records'])!=576 or len(result['flat_records'])!=32:
                raise ValueError('Expected576fine and32constant measurements')
            result['status']='completed_known_correction_tradeoff_not_qualification'
    except Exception as error:
        result.update(status='failed_diagnostic_execution',error=str(error))
        raise
    finally:
        result['memory']=guard.stats()
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result)
        save(root/'docs/validation/raw'/f'{identity}.json',result)
    print(result['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
