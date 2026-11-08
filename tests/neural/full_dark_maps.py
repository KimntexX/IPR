"""Stream full visible-sensor dark maps; no image exports or production changes."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import digest,read,write


def update_moments(mean,m2,values,count):
    if mean.shape!=m2.shape or values.shape!=mean.shape or count<1:
        raise ValueError('Matching arrays and positive updated count required')
    delta=values-mean
    mean+=delta/np.float32(count)
    m2+=delta*(values-mean)


def save_array(path,values):
    if path.exists():raise ValueError('Existing scientific array must not be overwritten')
    temporary=path.with_suffix('.tmp')
    try:
        with temporary.open('xb') as stream:np.save(stream,values,allow_pickle=False)
        temporary.replace(path)
    finally:temporary.unlink(missing_ok=True)
    return dict(path=str(path),sha256=digest(path),shape=list(values.shape),dtype=str(values.dtype))


def summarize_maps(cal_mean,cal_var,test_mean,test_var,cal_n,test_n):
    if not all(v.shape==cal_mean.shape for v in (cal_var,test_mean,test_var)):
        raise ValueError('Matching full sensor maps required')
    if min(cal_n,test_n)<2:raise ValueError('Two independent groups required')
    height,width=cal_mean.shape
    rows=[]
    hot_mask=np.zeros(cal_mean.shape,dtype=bool)
    for dy,dx in ((0,0),(0,1),(1,0),(1,1)):
        pedestal=float(np.median(cal_mean[dy::16,dx::16]))
        test_pedestal=float(np.median(test_mean[dy::16,dx::16]))
        count=0;sum_v=0.;sum_w=0.;sum_a=0.;sum_b=0.;sum_ab=0.
        sum_a2=0.;sum_b2=0.;hot_count=0;replicated=0
        for first in range(dy,height,256):
            a=cal_mean[first:min(first+256,height):2,dx::2].astype(np.float64)-pedestal
            b=test_mean[first:min(first+256,height):2,dx::2].astype(np.float64)-test_pedestal
            v=cal_var[first:min(first+256,height):2,dx::2].astype(np.float64)
            w=test_var[first:min(first+256,height):2,dx::2].astype(np.float64)
            if not all(np.isfinite(t).all() for t in (a,b,v,w)) or min(v.min(),w.min())<0:
                raise ValueError('Nonfinite or negative dark statistics')
            threshold=np.maximum(20.,10*np.sqrt(v/cal_n))
            candidate=np.abs(a)>threshold
            hot_mask[first:min(first+256,height):2,dx::2]=candidate
            hot_count+=int(candidate.sum())
            replicated+=int((candidate&(np.abs(b)>.5*threshold)&(np.sign(a)==np.sign(b))).sum())
            count+=a.size;sum_v+=float(v.sum());sum_w+=float(w.sum())
            sum_a+=float(a.sum());sum_b+=float(b.sum());sum_ab+=float((a*b).sum())
            sum_a2+=float((a*a).sum());sum_b2+=float((b*b).sum())
        av,bv=sum_a/count,sum_b/count
        covariance=sum_ab/count-av*bv
        row=dict(physical_position_yx=[dy,dx],pixels=count,calibration_pedestal_dn=pedestal,
            control_pedestal_dn=test_pedestal,calibration_temporal_variance_dn2=sum_v/count,
            control_temporal_variance_dn2=sum_w/count,
            shared_spatial_cross_covariance_dn2=covariance,
            calibration_shared_variance_debiased_dn2=sum_a2/count-av*av-sum_v/count/cal_n,
            control_shared_variance_debiased_dn2=sum_b2/count-bv*bv-sum_w/count/test_n,
            atypical_mean_candidates=hot_count,replicated_candidates=replicated,
            candidate_rule='Absolute calibration pedestal deviation > max(20DN,10SEM); not a calibrated defect label')
        rows.append(row)
    return rows,hot_mask


def run(root,parent,output):
    import rawpy
    from memory_budget import MemoryGuard
    qualification=read(parent)
    if qualification['status']!='passed_dark_repeatability_component_not_full_bench':
        raise ValueError('Repeated dark component must pass before map construction')
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(path=str(parent),sha256=digest(parent)),groups=[],
        tools={n:digest(Path(__file__).with_name(n)) for n in ('full_dark_maps.py','memory_budget.py','prepare_next_training.py')},
        policy='Same frozen chronological split; full visible uint16 DN, float32 online moments, no clipping or correction',
        limits=['Pedestal candidates and repeatability are not a defect correction applied to images',
                'Full-frame maps apply to this camera/readout/exposure/session, not all conditions',
                'Passing map construction does not qualify light-dependent noise, references or overall training'])
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    try:
        with guard:
            for group in qualification['groups']:
                iso=group['iso'];maps={};geometry=None
                for role in ('calibration','control'):
                    sources=[r for r in group['source_identities'] if r['role']==role]
                    mean=m2=None
                    for index,source in enumerate(sources,1):
                        guard.check()
                        path=Path(source['path'])
                        if digest(path)!=source['sha256']:raise ValueError('RAW source identity changed')
                        with rawpy.imread(str(path)) as raw:
                            frame=raw.raw_image_visible
                            current=dict(shape=list(frame.shape),origin_xy=[raw.sizes.left_margin,raw.sizes.top_margin],
                                         pattern=raw.raw_colors_visible[:2,:2].tolist())
                            if geometry is None:geometry=current
                            if current!=geometry:raise ValueError('Sensor geometry/phase differs between frames')
                            if mean is None:
                                mean=np.zeros(frame.shape,np.float32);m2=np.zeros(frame.shape,np.float32)
                            update_moments(mean,m2,frame,index)
                            del frame
                        result['active']=dict(iso=iso,role=role,completed_frames=index,total=len(sources))
                        write(output/'progress.json',result)
                    m2/=np.float32(len(sources)-1)
                    maps[role+'_mean']=save_array(output/f'iso{iso}-{role}-mean.npy',mean)
                    maps[role+'_variance']=save_array(output/f'iso{iso}-{role}-variance.npy',m2)
                    del mean,m2
                opened={k:np.load(v['path'],mmap_mode='r',allow_pickle=False) for k,v in maps.items()}
                try:
                    summary,mask=summarize_maps(opened['calibration_mean'],opened['calibration_variance'],
                        opened['control_mean'],opened['control_variance'],group['calibration_frames'],group['control_frames'])
                    maps['atypical_calibration_mean_mask']=save_array(output/f'iso{iso}-atypical-mask.npy',mask)
                    del mask
                finally:
                    for array in opened.values():
                        if getattr(array,'_mmap',None) is not None:array._mmap.close()
                result['groups'].append(dict(iso=iso,geometry=geometry,calibration_frames=group['calibration_frames'],
                    control_frames=group['control_frames'],maps=maps,summary=summary))
                write(output/'progress.json',result)
            result['status']='completed_full_dark_maps_not_full_physical_benchmark'
    except Exception as error:
        result.update(status='stopped',error=type(error).__name__+': '+str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-full-dark-maps-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.parent,a.output)
