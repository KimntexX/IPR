"""Estimate variance contribution to coherent training loss; no learning."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import time

import torch

from audit_spectral_noise import source_contract
from bayer_canonical import canonicalize
from campaign_artifacts import campaign_models
from context_data import extend_texture,extend_observation
from diagnose_multitone_training import select_events
from diagnose_train_learnability import digest,save
from fine_texture_training import texture
from joint_bayer_unet import sample_rgb
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from noisy_moment_unet import NoisyMomentUNet
from paired_structure_loss import coherent_projection
from run_noisy_moment_pilot import observation,NOISE
from run_synthetic_joint_pilot import tensor_hash
from synthetic_bayer_data import PATTERNS


def decompose(projections):
    if projections.ndim != 2 or projections.shape[0] < 2 or not torch.isfinite(projections).all():
        raise ValueError('Finite draw by plane projection matrix required')
    q = projections.double()
    variance = q.var(dim=0,unbiased=True)
    mean = q.mean(dim=0)
    bias_squared = mean.square()-variance/len(q)
    return dict(mean=mean.tolist(),sample_variance=variance.tolist(),bias_squared_estimate=bias_squared.tolist(),
        mean_square=q.square().mean(dim=0).tolist(),
        all_pairs_cross=((q.sum(dim=0).square()-q.square().sum(dim=0))/(len(q)*(len(q)-1))).tolist())


def run(root,output,prepared):
    base = root/'build-patch_neural_restoration'
    if not all(p.resolve().is_relative_to(base.resolve()) for p in (output,prepared)):
        raise ValueError('Dedicated neural paths required')
    plans = {arm:json.loads((prepared/arm/'plan.json').read_text(encoding='utf-8')) for arm in ('control','correlation')}
    selected = select_events(plans['control']['texture_augmentation']['events'])
    if selected != select_events(plans['correlation']['texture_augmentation']['events']):
        raise ValueError('Paired training contents must match')
    now = datetime.now(timezone.utc)
    identity = 'neural-paired-bias-study-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result = dict(schema_version=1,campaign_id=identity,measured_at_utc=now.isoformat(),status='incomplete',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('study_paired_structure.py','paired_structure_loss.py')},
        selection='First2 strong training events per family/frequency bin, seeds71/73, both arms, two positive noises, eight fresh independent draws.',
        selected_events=selected,records=[],
        limits=['No training; estimate of a proposed fixed-reference projection, not the original observed-sigma loss.',
            'Eight draws, finite-sample bias-squared estimates may be negative. Planes and contents are not independent replicates.',
            'All-pairs terms reuse draws:28pairs are not28independent observations.',
            'Clean target used only for proposed training loss/audit, never inference. No reserved data opened.'])
    output.mkdir(parents=True,exist_ok=False)
    guard,started = MemoryGuard(**plans['control']['memory_policy']),time.monotonic()
    try:
        with guard,torch.inference_mode():
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            for arm,plan in plans.items():
                source = source_contract(root,json.loads((base/f'input-cues-{arm}-protected-v001/result.json').read_text(encoding='utf-8')))
                check_inference(plan,source['tools'])
                folders = campaign_models(base/f'input-cues-{arm}-campaign-v001')
                for seed,folder in zip((71,73),folders):
                    weights = folder/'weights.pt'
                    proof = json.loads((folder/'result.json').read_text(encoding='utf-8'))
                    if digest(weights) != proof['weights_sha256']:
                        raise ValueError('Final weights changed')
                    model = NoisyMomentUNet(8).eval().requires_grad_(False)
                    model.load_state_dict(torch.load(weights,weights_only=True)['state_dict'],strict=True)
                    for event in selected:
                        centre,_ = texture(event['family'],event['serial'],128)
                        if tensor_hash(centre) != event['target_sha256']:
                            raise ValueError('Training target changed')
                        expanded = extend_texture(event['family'],event['serial'],256)
                        tags = torch.tensor([PATTERNS[event['phase']]])
                        raw_clean = sample_rgb(expanded,tags)
                        for level in (1,2):
                            a,b = NOISE[level]
                            clean,reference_sigma,_,_ = canonicalize(raw_clean/2,(a*raw_clean.clamp_min(0)+b).sqrt()/2,tags)
                            projections,seeds = [],[]
                            for replicate in range(8):
                                guard.check()
                                noise_seed = 310000000+event['serial']*32+level*8+replicate
                                values,sigma = observation(centre,tags,level,noise_seed)
                                values,sigma = extend_observation(expanded,tags,values,sigma,a,b,320000000+noise_seed)
                                values,sigma,labels,_ = canonicalize(values/2,sigma/2,tags)
                                corrected = model.correct_observations(values,sigma,labels)
                                projections.append(coherent_projection(corrected,clean,reference_sigma,56)[0])
                                seeds.append(noise_seed)
                            q = torch.stack(projections)
                            result['records'].append(dict(arm=arm,seed=seed,serial=event['serial'],family=event['family'],
                                frequency=event['frequency'],amplitude=event['amplitude'],noise_index=level,
                                noise_seeds=seeds,projection_by_draw=q.tolist(),decomposition=decompose(q),
                                weights_sha256=proof['weights_sha256']))
                    print('PAIRED BIAS',arm,seed,flush=True)
            if len(result['records']) != 128:
                raise ValueError('Expected128conditional series')
            result['status'] = 'completed_known_paired_bias_feasibility_not_qualification'
    except Exception as error:
        result.update(status='failed_diagnostic_execution',error=str(error))
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        result['memory'] = guard.stats()
        save(output/'result.json',result)
        save(root/'docs/validation/raw'/f'{identity}.json',result)
    print(result['status'],flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--prepared',type=Path,required=True)
    args = parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output,args.prepared)
