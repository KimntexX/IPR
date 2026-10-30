"""Research-only contextual CFA variance and empirical signed-RCD transport.

Only explicit characterized camera/ISO entries are selected. No content MAD,
ISO interpolation, borrowed camera, runtime configuration, or production cache.
"""
import hashlib
import json
import cv2
import numpy as np
from audit_signal_noise_bridge import PHASES,normalize_coefficients
from study_cfa_signal_transport import native

ALIASES={'EOS R6 Mark III':'Canon EOS R6 Mark III','Canon EOS R6 Mark III':'Canon EOS R6 Mark III'}
STAGE='balanced_signed_camera_rgb_after_RCD_opening_median_before_LCP_DCP'


def prepare_context(catalog,catalog_sha,camera,iso,applied,region=1):
    if catalog.get('verdict')!='audited_frozen_CFA_law_and_signed_domain_not_production_noise_profile':
        raise ValueError('Unqualified characterization')
    if camera not in ALIASES:raise ValueError('Uncharacterized camera')
    if not np.isfinite(iso) or int(iso)!=iso:raise ValueError('ISO must be an exact characterized integer')
    camera=ALIASES[camera]
    rows=[m for m in catalog['models'] if m['camera']==camera and m['iso']==iso]
    if len(rows)!=4 or {r['phase'] for r in rows}!=set(PHASES):raise ValueError('Uncharacterized ISO or duplicate phase')
    gains=applied['gains'];black=applied['black'];white=applied['white'];divisor=applied['divisor']
    if (len(gains)!=4 or len(black)!=4 or applied['policy']!='signed'
        or not np.isfinite(white) or white<=0 or not np.isfinite(divisor) or divisor<=0
        or not np.isfinite(black).all() or min(black)<0 or max(black)>=white):raise ValueError('Invalid signed normalization')
    context=dict(camera=camera,iso=int(iso),catalog_sha256=catalog_sha,region=region,
                 gains=list(gains),black=list(black),white=white,divisor=divisor,stage=STAGE,phases={})
    for row in rows:
        phase=row['phase'];colour=PHASES[phase][2]
        anchor=next((r for r in row['regions'] if r['roi']==region),None)
        if anchor is None:raise ValueError('Uncharacterized sensor region')
        a=row['slope_dn'];b=anchor['temporal_dark_variance_dn2'];reference=anchor['reference_black_dn']
        gain=gains[colour];runtime=black[colour]
        A,B=normalize_coefficients(a,b,reference,runtime,gain,divisor)
        context['phases'][phase]=dict(a=a,b=b,reference_black=reference,runtime_black=runtime,gain=gain,
            A=A,B=B,zero_signal_mean=gain*(reference-runtime)/divisor,fit_range_dn=row['fit_signal_range_dn'])
    return context


def phase_budget(context,mean_rgb):
    mean=np.asarray(mean_rgb,dtype=float)
    if mean.shape!=(3,) or not np.isfinite(mean).all():raise ValueError('Invalid local camera mean')
    output={}
    for phase,(_,_,colour) in PHASES.items():
        p=context['phases'][phase];value=mean[1 if colour==3 else colour]
        signal=value*context['divisor']/p['gain']+p['runtime_black']-p['reference_black']
        nonnegative=max(0.,float(signal))
        output[phase]=dict(signal_dn=float(signal),variance_dn2=p['b']+p['a']*nonnegative,
            variance_balanced=(p['b']+p['a']*nonnegative)*(p['gain']/context['divisor'])**2,
            below_zero_latent=bool(signal<0),fit_extrapolation=bool(signal<p['fit_range_dn'][0] or signal>p['fit_range_dn'][1]))
    return output


def mosaic(rgb):
    if rgb.shape!=(256,256,3) or not np.isfinite(rgb).all():raise ValueError('Invalid nominal RGB')
    output=np.empty((256,256))
    for _,(dy,dx,colour) in PHASES.items():output[dy::2,dx::2]=rgb[dy::2,dx::2,1 if colour==3 else colour]
    return output.astype('<f4')


def simulate_context(context,nominal,read_dn,rng,distribution):
    if nominal.shape!=(256,256) or read_dn.shape!=nominal.shape or not np.isfinite(nominal).all() or not np.isfinite(read_dn).all():
        raise ValueError('Invalid CFA inputs')
    output=nominal.astype(float).copy()
    for phase,(dy,dx,_) in PHASES.items():
        p=context['phases'][phase];scale=p['gain']/context['divisor']
        signal=np.maximum(0.,nominal[dy::2,dx::2]/scale+p['runtime_black']-p['reference_black'])
        if distribution=='gaussian':shot=np.sqrt(p['a']*signal)*rng.normal(size=signal.shape)
        elif distribution=='scaled_poisson':
            if p['a']<=0:raise ValueError('Poisson needs positive slope')
            rate=signal/p['a'];shot=p['a']*(rng.poisson(rate)-rate)
        else:raise ValueError('Unknown distribution')
        output[dy::2,dx::2]+=scale*(read_dn[dy::2,dx::2]+shot)
    return output.astype('<f4')


def local_mean(observed,roi):
    if observed.shape!=(256,256,3) or not np.isfinite(observed).all():raise ValueError('Invalid observed RGB')
    y,x,height,width=roi
    if min(x,y)<0 or min(height,width)<=0 or y+height>256 or x+width>256:raise ValueError('Invalid guide ROI')
    # Only a first-moment guide, never residual variance/MAD or clean truth.
    guide=cv2.boxFilter(observed.astype(float),-1,(31,31),normalize=True,borderType=cv2.BORDER_REFLECT_101)
    return guide[y:y+height,x:x+width].mean(axis=(0,1))


def band_covariances(residual,roi):
    if residual.shape!=(256,256,3) or not np.isfinite(residual).all():raise ValueError('Invalid residual')
    y,x,height,width=roi
    if min(x,y)<0 or min(height,width)<2 or y+height>256 or x+width>256:raise ValueError('Invalid covariance ROI')
    current=residual.astype(float)@np.array([[.25,.5,.25],[.5,0,-.5],[-.5,1,-.5]]).T
    result=[]
    for level in range(5):
        step=1<<level;kernel=np.zeros(4*step+1);kernel[::step]=np.array([1,4,6,4,1])/16
        base=cv2.sepFilter2D(current,-1,kernel,kernel,borderType=cv2.BORDER_REFLECT_101)
        values=(current-base)[y:y+height,x:x+width].reshape(-1,3)
        result.append(np.cov(values,rowvar=False,ddof=1));current=base
    return np.array(result)


def profile_key(context,mean_rgb,reference_sha,tool_sha,seeds,distribution,roi=(64,64,128,128)):
    payload=dict(context=context,mean_rgb=list(map(float,mean_rgb)),reference_sha=reference_sha,
                 tool_sha=tool_sha,seeds=list(seeds),distribution=distribution,roi_yxhw=list(roi),recipe='context-flat-B3-v2')
    return hashlib.sha256(json.dumps(payload,sort_keys=True,allow_nan=False).encode()).hexdigest()


def prepare_profile(context,mean_rgb,read_dn,tool,env,seeds,distribution,roi=(64,64,128,128)):
    if not seeds:raise ValueError('Empty calibration seeds')
    target=mosaic(np.broadcast_to(np.asarray(mean_rgb),(256,256,3)))
    clean,_=native(tool,target,env)
    profiles=[]
    for seed in seeds:
        sample=simulate_context(context,target,read_dn,np.random.default_rng(np.random.SeedSequence([seed,context['iso']])),distribution)
        output,_=native(tool,sample,env)
        profiles.append(band_covariances(output['after_median']-clean['after_median'],roi))
    return dict(mean_rgb=list(map(float,mean_rgb)),phase_budget=phase_budget(context,mean_rgb),
                roi_yxhw=list(roi),
                covariance=np.mean(profiles,axis=0).tolist())
