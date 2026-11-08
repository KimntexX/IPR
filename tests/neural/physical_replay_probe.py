"""Test a fixed Poisson-shot + empirical dark-pair surrogate on known flat pairs.

Marginal temporal diagnostics only: fixed pattern cancels in differences.
This does not qualify the complete natural-photo simulator or trigger training.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import digest,read,write


def differences(read_frames,signal_dn,gain_dn,seed,repetitions=8):
    if (read_frames.ndim!=3 or len(read_frames)<2 or not np.isfinite(read_frames).all()
            or not np.isfinite([signal_dn,gain_dn]).all() or min(signal_dn,gain_dn)<0 or repetitions<2):
        raise ValueError('Finite dark library, nonnegative shot law and >=2 repetitions required')
    rng=np.random.default_rng(seed)
    first=rng.integers(len(read_frames),size=repetitions)
    second=(first+rng.integers(1,len(read_frames),size=repetitions))%len(read_frames)
    shape=(repetitions,*read_frames.shape[-2:])
    if gain_dn>0:
        a=np.rint(gain_dn*rng.poisson(signal_dn/gain_dn,size=shape))
        b=np.rint(gain_dn*rng.poisson(signal_dn/gain_dn,size=shape))
    else:a=b=np.full(shape,signal_dn)
    # Empirical dark values already include readout and ADC quantization.
    # Separate shot rounding is an approximation, validated rather than hidden.
    return a-b+read_frames[first].astype(np.float64)-read_frames[second].astype(np.float64)


def metrics(delta):
    if delta.ndim==2:delta=delta[None]
    centered=delta-delta.mean(axis=(-2,-1),keepdims=True)
    power=float((centered**2).mean())
    height,width=centered.shape[-2:]
    window=np.outer(np.hanning(height),np.hanning(width))
    psd=np.mean(np.abs(np.fft.fft2(centered*window,axes=(-2,-1)))**2,axis=0)
    fy,fx=np.meshgrid(np.fft.fftfreq(height),np.fft.fftfreq(width),indexing='ij')
    radius=np.sqrt(fx*fx+fy*fy)
    low=float(psd[(radius>0)&(radius<.1)].mean());high=float(psd[(radius>=.25)&(radius<=.5)].mean())
    return dict(pair_variance_dn2=float(np.mean(np.var(delta,axis=(-2,-1),ddof=1))/2),
        lag_x1=float(np.mean(centered[:,:,:-1]*centered[:,:,1:])/max(power,1e-30)),
        lag_y1=float(np.mean(centered[:,:-1,:]*centered[:,1:,:])/max(power,1e-30)),
        low_high_psd_ratio=low/max(high,1e-30))


def run(root,output):
    from memory_budget import MemoryGuard
    build=root/'build-patch_neural_restoration'
    old_path=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    new_path=build/'new-dark-qualification-v001/progress.json'
    remaining_path=build/'remaining-dark-component-v001/progress.json'
    old,new,remaining=map(read,(old_path,new_path,remaining_path))
    sources={}
    for group in new['groups']:
        cached=group['cached_dn'];path=Path(cached['path'])
        if digest(path)!=cached['sha256']:raise ValueError('Dark source arrays changed')
        sources[group['iso']]=np.load(path,mmap_mode='r',allow_pickle=False)[:32]
    for group in remaining['groups']:
        selected=[r for r in group['source_identities'] if r['role']=='calibration']
        frames=[]
        for source in selected:
            frame=next(r for r in old['frames'] if r['source_sha256']==source['sha256'])
            path=root/frame['cached_tiles']
            if digest(path)!=frame['tiles_sha256']:raise ValueError('Old dark source changed')
            frames.append(np.load(path,allow_pickle=False))
        sources[group['iso']]=np.stack(frames)
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),rows=[],
        parents=[dict(path=str(p),sha256=digest(p)) for p in (old_path,new_path,remaining_path)],
        tool_sha256=digest(Path(__file__)),
        criteria=dict(variance_median=.05,variance_q90=.15,lag_median=.02,lag_q90=.05,psd_median=.10,psd_q90=.25),
        limits=['Known old variance-fit heldout observations, not fresh independent final test',
                'No coefficient refit or control-selected source/threshold change',
                'Constant local signal and empirical read pairs; full fixed pattern cancels here',
                'Additional shot rounding and lack of ADC saturation are approximations',
                'No natural content, joint colour, sigma estimator, comparator or full-model qualification'])
    write(output/'freeze.json',{k:v for k,v in result.items() if k!='rows'})
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    try:
        with guard:
            frames=old['frames'];pairs={r['index']:r for r in old['pairs']}
            for fit in old['fits']:
                iso,plane=fit['iso'],fit['plane']
                if iso not in sources:continue
                gain=fit['fit'].get('shot_slope_dn')
                if gain is None:continue
                for field in fit['fit'].get('heldout',[]):
                    guard.check()
                    pair,roi=field['pair'],field['roi']
                    observed=[]
                    for index in pairs[pair]['frames']:
                        frame=frames[index];path=root/frame['cached_tiles']
                        observed.append(np.load(path,mmap_mode='r',allow_pickle=False)[roi,plane].astype(np.float64))
                    real=metrics(observed[0]-observed[1])
                    simulated=metrics(differences(sources[iso][:,roi,plane],field['signal_dn'],gain,
                                                 712000+pair*100+roi*4+plane))
                    result['rows'].append(dict(iso=iso,plane=plane,pair=pair,roi=roi,signal_dn=field['signal_dn'],
                        shot_gain_dn=gain,observed=real,simulated=simulated,
                        relative_variance_error=abs(simulated['pair_variance_dn2']-real['pair_variance_dn2'])/real['pair_variance_dn2'],
                        largest_lag_difference=max(abs(simulated[k]-real[k]) for k in ('lag_x1','lag_y1')),
                        relative_psd_ratio_error=abs(simulated['low_high_psd_ratio']-real['low_high_psd_ratio'])/real['low_high_psd_ratio']))
            result['assessment']=[]
            for iso in sorted({r['iso'] for r in result['rows']}):
                rows=[r for r in result['rows'] if r['iso']==iso];values={}
                for name,key in (('variance','relative_variance_error'),('lag','largest_lag_difference'),('psd','relative_psd_ratio_error')):
                    sample=[r[key] for r in rows];values[name+'_median']=float(np.median(sample));values[name+'_q90']=float(np.quantile(sample,.9))
                result['assessment'].append(dict(iso=iso,fields=len(rows),values=values,
                    passed=all(values[k]<=limit for k,limit in result['criteria'].items())))
            result['status']='passed_marginal_probe_not_full_bench' if all(a['passed'] for a in result['assessment']) else 'physical_replay_probe_guard_not_met'
    except Exception as error:result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-replay-probe-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
