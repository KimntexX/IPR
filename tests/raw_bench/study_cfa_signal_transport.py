"""Frozen empirical CFA variance law -> signed RCD/median -> opponent/B3 covariance.

Read residual is real; added signal noise is independent Gaussian moment-matched,
not a calibrated physical photon distribution. No denoising or DCP here.
"""
import argparse
import base64
from datetime import datetime,timezone
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
from capture import load_capture
from audit_signal_noise_bridge import PHASES
from study_structure_prediction import authenticated_selection
from study_structure_amplitude_free import template


CALIBRATION_SEEDS=(513,733,953,1173)
CONTROL_SEEDS=(1393,1613,1833,2053)
LEVELS={1600:(.003,.03),51200:(.03,.1)}
ROI=np.s_[64:192,64:192]
OPPONENT=np.array([[.25,.5,.25],[.5,0,-.5],[-.5,1,-.5]])


def camera_planes(rgb):
    current=rgb@OPPONENT.T
    fields={'pixel':current}
    for level in range(3):
        step=1<<level
        kernel=np.zeros(4*step+1);kernel[::step]=np.array([1,4,6,4,1])/16
        base=cv2.sepFilter2D(current,-1,kernel,kernel,borderType=cv2.BORDER_REFLECT_101)
        fields[f'B3_{level}']=current-base
        current=base
    return fields


def covariance(field):
    values=field[ROI].reshape(-1,3)
    return np.cov(values,rowvar=False,ddof=1)


def measure(noisy,clean):
    residual=noisy-clean
    fields=camera_planes(residual)
    result={name:dict(covariance=covariance(field).tolist(),mean=field[ROI].reshape(-1,3).mean(axis=0).tolist())
            for name,field in fields.items()}
    result['rgb_mse_to_clean_rcd']=float(np.mean(residual[ROI]**2))
    result['negative_fraction_rgb']=np.mean(noisy[ROI]<0,axis=(0,1)).tolist()
    return result


def simulate(target,read_dn,slopes,gains,divisor,normal):
    """No dark-variance b argument: the actual dark residual already contains read noise."""
    if (target.shape!=(256,256) or read_dn.shape!=target.shape or normal.shape!=target.shape
        or not all(np.isfinite(v).all() for v in (target,read_dn,normal)) or np.min(target)<0):
        raise ValueError('Invalid latent/read/normal input')
    if not np.isfinite(divisor) or divisor<=0:
        raise ValueError('Invalid divisor')
    shot=np.zeros_like(target);balanced_read=np.zeros_like(target);predicted_shot=np.zeros_like(target)
    for phase,(dy,dx,_) in PHASES.items():
        a,g=slopes[phase],gains[phase]
        if not np.isfinite(a) or a<0 or not np.isfinite(g) or g<=0:
            raise ValueError('Invalid phase law or gain')
        signal_dn=target[dy::2,dx::2]*divisor/g
        shot_variance=a*signal_dn
        scale=g/divisor
        shot[dy::2,dx::2]=np.sqrt(shot_variance)*normal[dy::2,dx::2]*scale
        predicted_shot[dy::2,dx::2]=shot_variance*scale**2
        balanced_read[dy::2,dx::2]=read_dn[dy::2,dx::2]*scale
    observed=(target+balanced_read+shot).astype('<f4')
    return observed,balanced_read,shot,predicted_shot


def native(tool,cfa,env):
    pixels=np.asarray(cfa,dtype='<f4').tobytes()
    payload=dict(cfa=base64.b64encode(pixels).decode('ascii'),pattern=[[0,1],[1,2]])
    run=subprocess.run([str(tool.resolve())],input=json.dumps(payload)+'\n',capture_output=True,text=True,
                       encoding='utf-8',env=env,timeout=30)
    if run.returncode:
        raise RuntimeError(f'Signed transport failed: {run.stderr}')
    data=json.loads(run.stdout)
    if data['shape']!=[256,256,3] or data['dtype']!='f32_le':
        raise ValueError('Unexpected transport shape/units')
    output={}
    hashes={}
    for stage in ('before_median','after_median'):
        raw=base64.b64decode(data[stage],validate=True)
        if len(raw)!=256*256*3*4:
            raise ValueError('Truncated RGB result')
        image=np.frombuffer(raw,dtype='<f4').reshape(256,256,3).astype(float)
        if not np.isfinite(image).all():
            raise ValueError('Nonfinite RGB result')
        output[stage]=image;hashes[stage]=hashlib.sha256(raw).hexdigest()
    return output,dict(input_sha256=hashlib.sha256(pixels).hexdigest(),outputs_sha256=hashes,workers=data['workers'])


def mean_profile(rows):
    return {stage:{plane:np.mean([r['measurements'][stage][plane]['covariance'] for r in rows],axis=0)
                   for plane in ('pixel','B3_0','B3_1','B3_2')}
            for stage in ('before_median','after_median')}


def compare_covariance(predicted,observed):
    predicted=np.asarray(predicted);observed=np.asarray(observed)
    scale=max(float(np.linalg.norm(observed)),1e-30)
    eig=np.linalg.eigvalsh(predicted)
    valid=bool(eig.min()>=-max(float(np.linalg.norm(predicted)),1e-30)*1e-9)
    return dict(predicted_covariance=predicted.tolist(),observed_covariance=observed.tolist(),
        positive_semidefinite=valid,min_eigenvalue=float(eig.min()),
        relative_frobenius_error=float(np.linalg.norm(predicted-observed)/scale),
        sigma_predicted_over_observed=[float(np.sqrt(predicted[c,c]/observed[c,c]))
            if predicted[c,c]>=0 and observed[c,c]>0 else None for c in range(3)])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge-proof',type=Path,required=True)
    parser.add_argument('--pilot-proof',type=Path,required=True)
    parser.add_argument('--tool',type=Path,required=True)
    parser.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=parser.parse_args();cv2.setNumThreads(1)
    root=Path(__file__).resolve().parents[2]
    bridge=json.loads(args.bridge_proof.read_text(encoding='utf-8'))
    if bridge['verdict']!='audited_frozen_CFA_law_and_signed_domain_not_production_noise_profile':
        raise ValueError('Bridge not qualified')
    if sha256(root/bridge['freeze_path'])!=bridge['freeze_sha256']:
        raise ValueError('Changed bridge freeze')
    selection=authenticated_selection(root,args.pilot_proof)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    names=['tests/raw_bench/study_cfa_signal_transport.py','tests/raw_bench/cfa_transport_probe.cpp',
           'tests/raw_bench/audit_signal_noise_bridge.py','src/raw/Demosaic.cpp','src/raw/Demosaic.h']
    freeze=dict(schema_version=1,created_utc=stamp,study='signal_dependent_CFA_to_B3_transport',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        bridge_sha256=sha256(args.bridge_proof),pilot_sha256=sha256(args.pilot_proof),selection=selection,
        tool_sha256=sha256(args.tool),numpy=np.__version__,opencv=cv2.__version__,
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in names},
        protocol=dict(levels=LEVELS,patterns=['flat','bounded_irregular_fine'],roi=[64,64,128,128],
            calibration_seeds=CALIBRATION_SEEDS,control_seeds=CONTROL_SEEDS,
            centering='Frozen development black mean, central historical ROI, per CFA phase; not per-shot centering.',
            signal_noise='Independent Gaussian per CFA site, variance a*latent_DN, seed includes ISO and dark-shot index.',
            read_noise='Actual unpacked DN minus frozen phase black; no synthetic b term added.',
            dark='One deterministic sample per dark, never counted as eight Monte Carlo draws.',
            predictors=['other-dark read-only','other-dark matched-level flat MC','affine covariance from other-dark dark+lowest-level flat MC'],
            decision='Frozen coefficients/no threshold search; diagnose covariance transfer, not denoise quality or physical validation.'),
        limits=['Four reused development dark frames; Gaussian photon-like term only moment-matched to empirical law, not calibrated photon counts.',
                'Same dark pattern reused across seeds, so MC draws do not create independent real acquisitions.',
                'Reference centering simulates black calibration at measured local black; no production black selection changed.',
                'Flat-profile transfer to texture may fail with nonlinear RCD/median; covariance need not be affine in level.',
                'Noise residual is relative to clean RCD/median, not ideal original RGB; no DCP or denoising evaluated.',
                'Python B3 f64 has same kernel/support but is not bit-exact C++ f32; affine matrices are never PSD-clamped.'])
    frozen=root/'docs/validation/raw'/f'cfa-signal-transport-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),
        freeze_sha256=sha256(frozen),rows=[],paired_diagnostics=[],predictions=[],contexts=[])
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started=time.monotonic()
    try:
        for pair in selection:
            iso=int(pair['label'].split('ISO')[1]);models={r['phase']:r for r in bridge['models'] if r['iso']==iso}
            slopes={phase:m['slope_dn'] for phase,m in models.items()}
            black={phase:m['regions'][1]['reference_black_dn'] for phase,m in models.items()}
            loaded=[]
            for capture in pair['captures']:
                manifest,stages=load_capture(capture['path'])
                keys=sorted([k for k in stages if k[0]=='unpacked_cfa'],key=lambda k:k[1][1])
                descriptor,dn=stages[keys[1]]
                if descriptor['context']['cfa']!=[[0,1],[3,2]] or manifest['mode']!='signed':
                    raise ValueError('Unqualified CFA capture')
                loaded.append((manifest,dn[...,0].astype(float)))
            if loaded[0][0]['applied']!=loaded[1][0]['applied']:
                raise ValueError('Different normalization contexts')
            gains={phase:loaded[0][0]['applied']['gains'][colour] for phase,(_,_,colour) in PHASES.items()}
            divisor=loaded[0][0]['applied']['divisor']
            reads=[]
            for _,dn in loaded:
                residual=dn.copy()
                for phase,(dy,dx,_) in PHASES.items():residual[dy::2,dx::2]-=black[phase]
                reads.append(residual)
            result['contexts'].append(dict(iso=iso,slopes=slopes,frozen_black=black,gains=gains,divisor=divisor,
                signal_ranges_dn={phase:m['fit_signal_range_dn'] for phase,m in models.items()},
                temporal_dark_variance_dn2={phase:m['regions'][1]['temporal_dark_variance_dn2'] for phase,m in models.items()}))
            fixtures=[(0.,'flat')]+[(level,pattern) for level in LEVELS[iso] for pattern in ('flat','bounded_irregular_fine')]
            for level,pattern in fixtures:
                if pattern=='flat':target=np.full((256,256),level)
                else:
                    original=(template('irregular_fine',1)-.03)
                    target=level+min(.0015,.25*level)*np.tanh(original)
                target=target.astype(np.float32).astype(float)
                clean,clean_identity=native(args.tool,target,env)
                controls={}
                for shot in range(2):
                    seeds=(None,) if level==0 else CALIBRATION_SEEDS+CONTROL_SEEDS if pattern=='flat' else CONTROL_SEEDS
                    for seed in seeds:
                        normal=np.zeros((256,256)) if seed is None else np.random.default_rng(np.random.SeedSequence([seed,shot,iso])).normal(size=(256,256))
                        noisy,read,extra,shot_budget=simulate(target,reads[shot],slopes,gains,divisor,normal)
                        output,identity=native(args.tool,noisy,env)
                        input_metrics={}
                        for phase,(dy,dx,_) in PHASES.items():
                            error=(noisy.astype(float)-target)[ROI][dy::2,dx::2]
                            r=read[ROI][dy::2,dx::2];s=extra[ROI][dy::2,dx::2]
                            latent=target[ROI][dy::2,dx::2]*divisor/gains[phase]
                            domain=models[phase]['fit_signal_range_dn']
                            input_metrics[phase]=dict(measured_variance=float(np.var(error,ddof=1)),
                                measured_read_variance=float(np.var(r,ddof=1)),expected_added_variance=float(np.mean(shot_budget[ROI][dy::2,dx::2])),
                                realized_added_variance=float(np.var(s,ddof=1)),read_added_cross=float(2*np.cov(r.ravel(),s.ravel())[0,1]),
                                latent_signal_range_dn=[float(latent.min()),float(latent.max())],
                                fit_domain_fraction=float(np.mean((latent>=domain[0])&(latent<=domain[1]))))
                        result['rows'].append(dict(iso=iso,level=level,pattern=pattern,shot=shot,seed=seed,
                            split='dark_anchor' if seed is None else 'calibration' if seed in CALIBRATION_SEEDS else 'control',
                            identity=identity,clean_identity=clean_identity,input_CFA=input_metrics,
                            input_negative_fraction=float(np.mean(noisy[ROI]<0)),
                            measurements={stage:measure(output[stage],clean[stage]) for stage in output}))
                        if seed is None or seed in CONTROL_SEEDS:controls[(shot,seed)]=output
                for seed in ((None,) if level==0 else CONTROL_SEEDS):
                    left,right=controls[(0,seed)],controls[(1,seed)]
                    result['paired_diagnostics'].append(dict(iso=iso,level=level,pattern=pattern,seed=seed,
                        note='Difference suppresses common structure; not a total visible-noise oracle.',
                        stages={stage:{plane:covariance(field).tolist() for plane,field in camera_planes((left[stage]-right[stage])/np.sqrt(2)).items()}
                                for stage in left}))
                print('ISO',iso,'level',level,pattern,'transported',flush=True)
            iso_rows=[r for r in result['rows'] if r['iso']==iso]
            for calibration_shot in range(2):
                control_shot=1-calibration_shot
                read_profile=mean_profile([r for r in iso_rows if r['level']==0 and r['shot']==calibration_shot])
                low_profile=mean_profile([r for r in iso_rows if r['level']==LEVELS[iso][0] and r['pattern']=='flat' and r['shot']==calibration_shot and r['split']=='calibration'])
                for level in LEVELS[iso]:
                    matched=mean_profile([r for r in iso_rows if r['level']==level and r['pattern']=='flat' and r['shot']==calibration_shot and r['split']=='calibration'])
                    for pattern in ('flat','bounded_irregular_fine'):
                        actual=mean_profile([r for r in iso_rows if r['level']==level and r['pattern']==pattern and r['shot']==control_shot and r['split']=='control'])
                        for stage in actual:
                            for plane,observed in actual[stage].items():
                                read_cov=read_profile[stage][plane]
                                affine=read_cov+(level/LEVELS[iso][0])*(low_profile[stage][plane]-read_cov)
                                result['predictions'].append(dict(iso=iso,level=level,pattern=pattern,calibration_shot=calibration_shot,
                                    control_shot=control_shot,stage=stage,plane=plane,models={
                                        'read_only':compare_covariance(read_cov,observed),
                                        'matched_flat':compare_covariance(matched[stage][plane],observed),
                                        'affine_from_low':compare_covariance(affine,observed)}))
        if len(result['rows'])!=100 or len(result['predictions'])!=128 or len(result['paired_diagnostics'])!=34:
            raise ValueError('Incomplete fixed transport design')
        result['verdict']='measured_signed_transport_simulation_not_physical_or_production_noise_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'cfa-signal-transport-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':
    raise SystemExit(main())
