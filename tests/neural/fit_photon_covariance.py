"""Fit shot-covariance moments on original training exposures only.

Phenomenological noise covariance, not a claim about a sensor/codec mechanism.
No held-out fit, gain refit, threshold search or neural training.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import digest,read,write


def covariances(delta):
    value=delta.astype(np.float64)
    value-=value.mean(axis=(-2,-1),keepdims=True)
    return dict(variance=np.mean(value**2,axis=(-2,-1)),
                x=np.mean(value[...,:,:-1]*value[..., :,1:],axis=(-2,-1)),
                y=np.mean(value[...,:-1,:]*value[...,1:,:],axis=(-2,-1)))


def fit_coefficients(observations):
    if not observations:raise ValueError('Training observations required')
    if not all(np.isfinite([r[k] for k in ('shot_variance','observed_x','observed_y','read_x','read_y')]).all()
               and r['shot_variance']>=0 for r in observations):
        raise ValueError('Finite moments and nonnegative shot variance required')
    energy=sum(r['shot_variance']**2 for r in observations)
    if energy<=0:raise ValueError('Positive shot energy required')
    x=sum(r['shot_variance']*(r['observed_x']-r['read_x']) for r in observations)/energy
    y=sum(r['shot_variance']*(r['observed_y']-r['read_y']) for r in observations)/energy
    lower=1-2*(abs(x)+abs(y))
    return dict(gamma_x=float(x),gamma_y=float(y),minimum_spectral_bound=float(lower),
                positive_spectrum=bool(lower>0),training_fields=len(observations),training_only=True)


def colour_innovations(values,gamma_x,gamma_y):
    if (values.ndim<2 or not np.isfinite(values).all() or not np.isfinite([gamma_x,gamma_y]).all()
            or 1-2*(abs(gamma_x)+abs(gamma_y))<=0):
        raise ValueError('Finite innovations and positive spectral covariance required')
    h,w=values.shape[-2:]
    spectrum=1+2*gamma_x*np.cos(2*np.pi*np.fft.fftfreq(w))[None,:]+2*gamma_y*np.cos(2*np.pi*np.fft.fftfreq(h))[:,None]
    return np.fft.ifft2(np.fft.fft2(values,axes=(-2,-1))*np.sqrt(spectrum),axes=(-2,-1)).real


def dark_sources(root):
    build=root/'build-patch_neural_restoration'
    bank=read(root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json')
    sources={}
    for group in read(build/'new-dark-qualification-v001/progress.json')['groups']:
        cache=group['cached_dn'];path=Path(cache['path'])
        if digest(path)!=cache['sha256']:raise ValueError('New dark cache changed')
        sources[group['iso']]=np.load(path,mmap_mode='r',allow_pickle=False)[:32]
    for group in read(build/'remaining-dark-component-v001/progress.json')['groups']:
        arrays=[]
        for source in group['source_identities']:
            if source['role']!='calibration':continue
            frame=next(r for r in bank['frames'] if r['source_sha256']==source['sha256'])
            path=root/frame['cached_tiles']
            if digest(path)!=frame['tiles_sha256']:raise ValueError('Old dark cache changed')
            arrays.append(np.load(path,allow_pickle=False))
        sources[group['iso']]=np.stack(arrays)
    return bank,sources


def run(root,output):
    from memory_budget import MemoryGuard
    bank,sources=dark_sources(root)
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        coefficients=[],training_observations=[],tools={n:digest(Path(__file__).with_name(n)) for n in (
            'fit_photon_covariance.py','memory_budget.py')},
        protocol='Original fit_gate training exposure split, all eligible points; variance slopes fixed; no heldout data in fitting',
        limits=['Stationary nearest-neighbour covariance surrogate, no causal mechanism asserted',
                'Fixed pattern and cross-plane covariance not qualified by these diagonal coefficients'])
    write(output/'freeze.json',result)
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    try:
        with guard:
            read_moments={}
            for iso,source in sources.items():
                values=source.astype(np.float64)
                values-=values.mean(axis=0,keepdims=True)
                m=covariances(values)
                read_moments[iso]={k:v.mean(axis=0)*len(values)/(len(values)-1) for k,v in m.items()}
            fit_by={(r['iso'],r['plane']):r['fit'] for r in bank['fits']}
            for pair in bank['pairs']:
                if pair['kind']!='light' or pair['iso'] not in sources:continue
                fields=[r for r in bank['measurements'] if r['pair']==pair['index'] and r.get('fit_gate',{}).get('eligible')
                        and r['fit_gate']['split']=='training']
                if not fields:continue
                guard.check();arrays=[]
                for index in pair['frames']:
                    frame=bank['frames'][index];path=root/frame['cached_tiles']
                    if digest(path)!=frame['tiles_sha256']:raise ValueError('Light cache changed')
                    arrays.append(np.load(path,allow_pickle=False).astype(np.float64))
                m=covariances(arrays[0]-arrays[1]);iso=pair['iso']
                for field in fields:
                    roi,plane=field['roi'],field['plane'];gain=fit_by[(iso,plane)].get('shot_slope_dn')
                    if gain is None:continue
                    result['training_observations'].append(dict(iso=iso,plane=plane,pair=pair['index'],roi=roi,
                        shot_variance=gain*field['fit_gate']['signal_dn'],observed_x=float(m['x'][roi,plane]/2),
                        observed_y=float(m['y'][roi,plane]/2),read_x=float(read_moments[iso]['x'][roi,plane]),
                        read_y=float(read_moments[iso]['y'][roi,plane])))
            for iso in sorted(sources):
                for plane in range(4):
                    selected=[r for r in result['training_observations'] if r['iso']==iso and r['plane']==plane]
                    result['coefficients'].append(dict(iso=iso,plane=plane,**fit_coefficients(selected)))
            result['status']='frozen_training_covariance_candidate_not_qualified'
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'result.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-photon-covariance-fit-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
