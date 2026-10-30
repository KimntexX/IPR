"""Frozen same-filter test of transported signal-dependent noise versus read-only.

Research only. Independent simulated flat reference is estimated by the CURRENT
pooled estimator, not an exact covariance oracle. No engine source edits.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import cv2
import numpy as np
from audit_dark_mean import sha256
from audit_signal_noise_bridge import PHASES
from capture import load_capture
from generate_noise_oracle import instrument
from study_cfa_signal_transport import simulate,native,LEVELS,CALIBRATION_SEEDS
from study_structure_prediction import authenticated_selection
from study_structure_amplitude_free import template
from study_noise_model_ablation import invoke
from signal_noise_draws import normalized_draws
from signal_noise_covariance import transported_covariance

PATTERNS=('flat','irregular_fine','irregular_coarse','novel_edge','color_edge')
CONTROL_SEEDS=(1393,1613)
REFERENCE_SEED=513
VARIANTS={'actual':'noisy_100','blind_pooled':'blind_pooled_noisy',
          'read_only':'read_pooled_noisy','signal_profile':'independent_noise_pooled_noisy',
          'same_noise_diagnostic':'same_noise_pooled_noisy'}


def encoded_rgb(image):
    if image.shape!=(256,256,3) or not np.isfinite(image).all():
        raise ValueError('Invalid RGB image')
    data=np.asarray(image,dtype='<f4').tobytes()
    return base64.b64encode(data).decode('ascii'),hashlib.sha256(data).hexdigest()


def fixture(level,pattern):
    if level<=0 or pattern not in PATTERNS:
        raise ValueError('Invalid fixture')
    amplitude=min(.0015,.25*level)
    if pattern=='color_edge':
        _,x=np.mgrid[:256,:256]
        edge=np.where(x>=192,1.,-1.)
        rgb=level+amplitude*edge[...,None]*np.array([1.,-.25,-.5])
        target=np.empty((256,256))
        for _,(dy,dx,c) in PHASES.items():
            target[dy::2,dx::2]=rgb[dy::2,dx::2,1 if c==3 else c]
    elif pattern=='flat':target=np.full((256,256),level)
    else:
        values=template(pattern,1)-.03
        if pattern=='novel_edge':values-=.5
        target=level+amplitude*np.tanh(values)
    return target.astype(np.float32).astype(float)


def summary(rows):
    result=[]
    variants_to_measure=dict(VARIANTS)
    if rows and all('external_covariance_noisy' in r['measurements'] for r in rows):
        variants_to_measure['external_covariance']='external_covariance_noisy'
    for iso,level,pattern in sorted({(r['iso'],r['level'],r['pattern']) for r in rows}):
        group=[r for r in rows if (r['iso'],r['level'],r['pattern'])==(iso,level,pattern)]
        variants={}
        for name,key in variants_to_measure.items():
            ratios=[r['measurements'][key]['rgb_mse']/r['measurements']['noisy_100']['rgb_mse'] for r in group]
            variants[name]=dict(median_mse_ratio_to_actual=float(np.median(ratios)),
                worst_mse_ratio_to_actual=max(ratios),
                median_mse_to_input=float(np.median([r['measurements'][key]['rgb_mse']/r['input_rgb_mse'] for r in group])))
        result.append(dict(iso=iso,level=level,pattern=pattern,cases=len(group),variants=variants))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--transport-proof',type=Path,required=True)
    p.add_argument('--bridge-proof',type=Path,required=True)
    p.add_argument('--pilot-proof',type=Path,required=True)
    p.add_argument('--transport-tool',type=Path,required=True)
    p.add_argument('--original-tool',type=Path,required=True)
    p.add_argument('--oracle-tool',type=Path,required=True)
    p.add_argument('--generated-source',type=Path,required=True)
    p.add_argument('--distribution',choices=('gaussian','scaled_poisson'),default='gaussian')
    p.add_argument('--external-covariance',action='store_true',help='Five measured B3 covariances from four calibration draws; one control seed, 40 cases.')
    p.add_argument('--covariance-draws',choices=(1,4),type=int,default=4,help='Attribution control: direct covariance of same single draw as MAD, or mean of four.')
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();cv2.setNumThreads(1)
    control_seeds=CONTROL_SEEDS[:1] if args.external_covariance else CONTROL_SEEDS
    root=Path(__file__).resolve().parents[2]
    parent=json.loads(args.transport_proof.read_text(encoding='utf-8'))
    if parent['verdict']!='measured_signed_transport_simulation_not_physical_or_production_noise_validation':
        raise ValueError('Invalid transport parent')
    parent_freeze=json.loads((root/parent['freeze_path']).read_text(encoding='utf-8'))
    if sha256(root/parent['freeze_path'])!=parent['freeze_sha256']:
        raise ValueError('Parent freeze changed')
    for name in ('src/raw/Demosaic.cpp','src/raw/Demosaic.h'):
        if sha256(root/name)!=parent_freeze['sources'][name]['sha256']:raise ValueError('Changed RCD')
    bridge=json.loads(args.bridge_proof.read_text(encoding='utf-8'))
    if sha256(args.bridge_proof)!=parent_freeze['bridge_sha256']:
        raise ValueError('Changed CFA law')
    if args.generated_source.read_text(encoding='utf-8')!=instrument((root/'src/raw/CameraDenoise.cpp').read_text(encoding='utf-8')):
        raise ValueError('Instrumented filter is not current exact hook')
    selection=authenticated_selection(root,args.pilot_proof)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/study_signal_filter.py','tests/raw_bench/study_cfa_signal_transport.py',
             'tests/raw_bench/structure_baseline_probe.cpp','tests/raw_bench/generate_noise_oracle.py',
             'tests/raw_bench/signal_noise_draws.py',
             'tests/raw_bench/signal_noise_covariance.py',
             'tests/raw_bench/noise_bench_control.cpp','tests/raw_bench/noise_bench_control.h',
             'src/raw/CameraDenoise.cpp','src/raw/CameraDenoise.h','src/raw/Demosaic.cpp']
    freeze=dict(schema_version=1,created_utc=stamp,study='signal_profile_same_filter',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={str(path):sha256(path) for path in (args.transport_proof,args.bridge_proof,args.pilot_proof)},
        tools={name:sha256(path) for name,path in (('transport',args.transport_tool),('original',args.original_tool),('instrumented',args.oracle_tool))},
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        selection=selection,protocol=dict(levels=LEVELS,patterns=PATTERNS,control_seeds=control_seeds,
            distribution=args.distribution,
            external_covariance=args.external_covariance,covariance_calibration_seeds=CALIBRATION_SEEDS[:args.covariance_draws],
            external_method=f'Mean centered empirical covariance of {args.covariance_draws} calibration residual(s) after RCD/median, on five B3 bands; current fromCovariance and filter unchanged.',
            reference_seed=REFERENCE_SEED,settings=[100,100],roi=[144,144,96,96],
            reference=f'Other real dark, independent {args.distribution} seed 513, flat at same latent CFA level, residual relative to clean RCD plus .03.',
            read_reference='Other real dark at zero latent signal, same normalization, residual plus .03.',
            current_estimator='MAD-based variants estimate current pooled noise on reference; external variant bypasses that estimation with measured covariance. No tuning or forced PSD.',
            clean_stress='Same nonzero estimated budget on clean output; not a realistic zero-noise input model.',
            gate='Compare MSE, opponent bias/MSE, gain on clean stress, all per case. No promotion on pooled averages; expose regressions.',
            parity='Every original/bypass noisy and clean output hash, every zero-model clean identity.'),
        limits=['Four reused darks; independent synthetic draws do not replace independent acquisitions.',
                f'Moment-matched {args.distribution} signal term, not full physical sensor distribution; ISO1600 level .03 extrapolates fit.',
                'Fixed 100/100 isolates model, not final user-strength law; no DCP, LCP, photo validation or latency budget.',
                'Matched reference contains signal-independent dark structure; pooled estimator can reject/underestimate it.',
                'Color and unique edges are new diagnostic cases, not an intact population validation.'])
    freeze['limits'].append('Scaled Poisson preserves a*signal variance but its assumed DN/e gain is not physically calibrated; distribution sensitivity only.')
    frozen=root/'docs/validation/raw'/f'signal-filter-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),freeze_sha256=sha256(frozen),
                rows=[],contracts=dict(pixel_parities=0,zero_identities=0))
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started=time.monotonic()
    try:
        for pair in selection:
            iso=int(pair['label'].split('ISO')[1]);models={r['phase']:r for r in bridge['models'] if r['iso']==iso}
            slopes={phase:m['slope_dn'] for phase,m in models.items()}
            black={phase:m['regions'][1]['reference_black_dn'] for phase,m in models.items()}
            reads=[];applied=None
            for capture in pair['captures']:
                manifest,stages=load_capture(capture['path'])
                keys=sorted([k for k in stages if k[0]=='unpacked_cfa'],key=lambda k:k[1][1])
                descriptor,dn=stages[keys[1]]
                if descriptor['context']['cfa']!=[[0,1],[3,2]] or manifest['mode']!='signed':raise ValueError('Unexpected CFA')
                if applied is not None and applied!=manifest['applied']:raise ValueError('Mismatched context')
                applied=manifest['applied'];residual=dn[...,0].astype(float)
                for phase,(dy,dx,_) in PHASES.items():residual[dy::2,dx::2]-=black[phase]
                reads.append(residual)
            gains={phase:applied['gains'][c] for phase,(_,_,c) in PHASES.items()};divisor=applied['divisor']
            read_reference=[]
            for shot in range(2):
                dark=simulate(np.zeros((256,256)),reads[shot],slopes,gains,divisor,np.zeros((256,256)))[0]
                output,_=native(args.transport_tool,dark,env)
                read_reference.append(output['after_median']+.03)
            for level in LEVELS[iso]:
                references=[];external_profiles=[]
                for shot in range(2):
                    target=fixture(level,'flat');clean,_=native(args.transport_tool,target,env)
                    normal=normalized_draws(np.random.default_rng(np.random.SeedSequence([REFERENCE_SEED,shot,iso])),
                        target,slopes,gains,divisor,args.distribution)
                    observed=simulate(target,reads[shot],slopes,gains,divisor,normal)[0]
                    output,_=native(args.transport_tool,observed,env)
                    references.append(output['after_median']-clean['after_median']+.03)
                    if args.external_covariance:
                        profiles=[transported_covariance(output['after_median']-clean['after_median'])]
                        for calibration_seed in CALIBRATION_SEEDS[1:args.covariance_draws]:
                            draw=normalized_draws(np.random.default_rng(np.random.SeedSequence([calibration_seed,shot,iso])),
                                target,slopes,gains,divisor,args.distribution)
                            noisy=simulate(target,reads[shot],slopes,gains,divisor,draw)[0]
                            transported,_=native(args.transport_tool,noisy,env)
                            profiles.append(transported_covariance(transported['after_median']-clean['after_median']))
                        external_profiles.append(np.mean(profiles,axis=0).tolist())
                for pattern in PATTERNS:
                    target=fixture(level,pattern);clean,_=native(args.transport_tool,target,env);truth=clean['after_median']
                    for shot in range(2):
                        for seed in control_seeds:
                            normal=normalized_draws(np.random.default_rng(np.random.SeedSequence([seed,shot,iso])),
                                target,slopes,gains,divisor,args.distribution)
                            observed=simulate(target,reads[shot],slopes,gains,divisor,normal)[0]
                            output,identity=native(args.transport_tool,observed,env);image=output['after_median']
                            payload={};hashes={}
                            for name,field in (('observed',image),('truth',truth),('reference',references[1-shot]),('read_reference',read_reference[1-shot])):
                                payload[name],hashes[name]=encoded_rgb(field)
                            if args.external_covariance:payload['covariance_profile']=external_profiles[1-shot]
                            original=invoke(args.original_tool,payload,env);measured=invoke(args.oracle_tool,payload,env)
                            for name in ('noisy_100','clean_100'):
                                if original[name]['pixels_sha256']!=measured[name]['pixels_sha256']:raise ValueError('Bypass parity failed')
                                result['contracts']['pixel_parities']+=1
                            if measured['zero_model_clean']['pixels_sha256']!=hashes['truth']:raise ValueError('Zero identity failed')
                            result['contracts']['zero_identities']+=1
                            roi=np.s_[144:240,144:240]
                            result['rows'].append(dict(iso=iso,level=level,pattern=pattern,shot=shot,seed=seed,
                                input_sha256=hashes,transport_identity=identity,
                                external_covariance=payload.get('covariance_profile'),
                                input_rgb_mse=float(np.mean((image[roi]-truth[roi])**2)),measurements=measured))
                    print(iso,level,pattern,'complete',len(result['rows']),f'/{40 if args.external_covariance else 80}',flush=True)
        if len(result['rows'])!=(40 if args.external_covariance else 80):raise ValueError('Incomplete design')
        result['summary']=summary(result['rows'])
        result['verdict']='measured_same_filter_signal_profiles_not_production_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'signal-filter-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':
    raise SystemExit(main())
