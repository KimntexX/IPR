"""Predeclared mathematical Haar study, no training or new model artifact."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import time
import torch
from torch.nn import functional as F
from audit_dru_projection import waves,cancellation_witness
from dru_haar_objective import local_haar_penalty,noise_aware_haar_penalty
from dru_structure_objective import structure_penalty
from dru_training_data import core_loss
from campaign_artifacts import sha256
from memory_budget import MemoryGuard


def scores(prediction,target,sigma,noise_aware=False):
    mse=float(core_loss(prediction,target,sigma,0))
    global_term=float(structure_penalty(prediction,target,0))
    haar_term=float(noise_aware_haar_penalty(prediction,target,sigma,0) if noise_aware else local_haar_penalty(prediction,target,0))
    return dict(mse=mse,global_projection=global_term,local_haar=haar_term,
        mse_plus_global=mse+global_term,mse_plus_haar=mse+haar_term)


def run(noise_aware=False):
    root=Path(__file__).resolve().parents[2]
    label='haar-noise-aware' if noise_aware else 'haar'
    output=root/f'build-patch_neural_restoration/dru-{label}-audit-v001'
    output.mkdir(exist_ok=False)
    plan=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),
        policy='Fixed three orthonormal Haar levels, three detail orientations, 8x8 coefficient tiles, equal group weights, epsilon1e-8, additive weight1. No coefficient tuning.',
        noise_sigmas=[.005,.015,.03],noise_seeds=list(range(45001,45017)),
        packed_cell_shifts=[[0,0],[0,1],[1,0],[1,1]],
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('audit_dru_haar.py','dru_haar_objective.py','dru_structure_objective.py','audit_dru_projection.py','dru_training_data.py')},
        gates=['Detect the previous explicit cancellation witness (nonzero Haar penalty).',
            'Reduce attenuation masking relative to pure-wave penalty for each declared added background.',
            'Exact clean target scores below noisy identity on all probes; finite outputs.'],
        limits=['Mathematical exploratory development study, not independent qualification.',
            'Passing the witnesses does not prove the trained model fixes them.',
            'Projections can still cancel within the same band/tile; a flat clean band provides no projection constraint.',
            'Circular shifts probe decimation phase on fixtures, not camera CFA phase or photographic boundary handling.',
            'No weights trained, no inference hook, no new photographs needed.'])
    plan['noise_aware']=noise_aware
    if noise_aware:
        plan['normalization']='Projection^2 / ((clean_energy + 1e-8)^2 + Var(projection)); independent CFA noise propagated through orthonormal Haar.'
        plan['limits'].append('Conditional variance assumes independent CFA noise; an estimated sigma or correlated post-demosaic noise need separate qualification.')
        prior=root/'build-patch_neural_restoration/dru-haar-audit-v001/result.json'
        prior_value=json.loads(prior.read_text(encoding='utf-8'))
        original=Path(__file__).with_name('dru_haar_objective.py').read_text(encoding='utf-8').split('\n\ndef noise_aware_haar_penalty')[0]
        if hashlib.sha256(original.encode('utf-8')).hexdigest()!=prior_value['plan']['tools']['dru_haar_objective.py']:
            raise ValueError('Original mathematical prototype source not recovered exactly')
        plan['ancestor']=dict(result_sha256=sha256(prior),original_objective_source=original)
    (output/'plan.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf-8')
    result=dict(status='started',plan=plan,masking=[],noise_probes=[])
    started=time.monotonic();guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    def check():
        guard.check()
        if time.monotonic()-started>120:raise TimeoutError('Two-minute CPU study budget')
    try:
        with guard,torch.inference_mode():
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            components,_=waves();weak=components[-1].float();base=.15+weak
            y,x=torch.meshgrid(torch.arange(128),torch.arange(128),indexing='ij')
            backgrounds={'none':torch.zeros_like(weak),'coarse_wave':components[0].float(),
                'edge':(.15*(x>63))[None,None].expand_as(weak),'three_waves':sum(components[:-1]).float()}
            for name,background in backgrounds.items():
                for dy,dx in plan['packed_cell_shifts']:
                    check();target=torch.roll(base+background,(dy,dx),(-2,-1))
                    prediction=target-.2*torch.roll(weak,(dy,dx),(-2,-1))
                    result['masking'].append(dict(background=name,shift=[dy,dx],**scores(prediction,target,torch.full_like(target,.01),noise_aware)))
            witness=cancellation_witness();target=(.15+sum(components)).float()
            predicted=target+sum(c*v for c,v in zip(components,witness['coefficient_changes'])).float()
            result['prior_cancellation']=dict(**scores(predicted,target,torch.full_like(target,.01),noise_aware),coefficient_changes=witness['coefficient_changes'])
            for name,target in [('flat',torch.full_like(base,.15)),('weak_wave',base),('weak_plus_edge',base+backgrounds['edge'])]:
                for sigma in plan['noise_sigmas']:
                    for seed in plan['noise_seeds']:
                        check();observed=target+torch.randn(target.shape,generator=torch.Generator().manual_seed(seed))*sigma
                        # Replicate boundary avoids injecting an artificial dark image border.
                        smooth=F.avg_pool2d(F.pad(observed,(1,1,1,1),mode='replicate'),3,stride=1)
                        predictions={'identity':observed,'oracle':target}
                        predictions.update({f'blur_{a}':observed+a*(smooth-observed) for a in (.25,.5,1.)})
                        measured={k:scores(v,target,torch.full_like(target,sigma),noise_aware) for k,v in predictions.items()}
                        result['noise_probes'].append(dict(case=name,sigma=sigma,seed=seed,scores=measured))
            pure={tuple(r['shift']):r for r in result['masking'] if r['background']=='none'}
            result['masking_effects']=[dict(background=r['background'],shift=r['shift'],
                global_retention=r['global_projection']/pure[tuple(r['shift'])]['global_projection'],
                haar_retention=r['local_haar']/pure[tuple(r['shift'])]['local_haar']) for r in result['masking'] if r['background']!='none']
            result['gates']=dict(cancellation_detected=result['prior_cancellation']['local_haar']>1e-10,
                less_masking_all_fixtures=all(r['haar_retention']>r['global_retention'] for r in result['masking_effects']),
                oracle_better_than_identity=all(r['scores']['oracle']['mse_plus_haar']<r['scores']['identity']['mse_plus_haar'] for r in result['noise_probes']))
            result['status']='completed_haar_math_study_not_training_qualification'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        (output/'result.json').write_text(json.dumps(result,indent=2,allow_nan=False),encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        canonical=root/'docs/validation/raw'/f'neural-dru-{label}-audit-v001-{stamp}.json'
        with canonical.open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        print(canonical,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--noise-aware',action='store_true')
    args=parser.parse_args();run(args.noise_aware)
