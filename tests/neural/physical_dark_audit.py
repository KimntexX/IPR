"""Read-only physical dark-noise diagnostics on the existing frozen DN bank.

No neural score, synthetic refit, black clipping or new reserve opening.
"""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import json
from pathlib import Path

import numpy as np
from prepare_next_training import digest, read, write


def statistics(frames):
    if frames.ndim!=5 or len(frames)<2 or frames.shape[2]!=4 or min(frames.shape[-2:])<16 or not np.isfinite(frames).all():
        raise ValueError('At least two finite T,ROI,4,H,W frames required')
    values=frames.astype(np.float64)
    count,rois,colours,height,width=values.shape
    average=values.mean(axis=0)
    residual=values-average
    temporal=(residual**2).sum(axis=0)/(count-1)
    variance=temporal.mean(axis=(-2,-1))
    shared_spatial=average.var(axis=(-2,-1),ddof=1)-variance/count
    drift=values.mean(axis=(-2,-1))
    noise=residual-residual.mean(axis=(-2,-1),keepdims=True)
    rows=[]
    for roi in range(rois):
        for plane in range(colours):
            r=noise[:,roi,plane]
            power=float((r*r).mean())
            correlations={}
            for step in (1,2,4):
                correlations[f'lag_x_{step}']=float(np.mean(r[:,:,:-step]*r[:,:,step:])/max(power,1e-30))
                correlations[f'lag_y_{step}']=float(np.mean(r[:,:-step,:]*r[:,step:,:])/max(power,1e-30))
            row_var=float(np.mean(r.mean(axis=-1)**2))
            col_var=float(np.mean(r.mean(axis=-2)**2))
            window=np.outer(np.hanning(height),np.hanning(width))
            psd=np.mean(np.abs(np.fft.fft2(r*window,axes=(-2,-1)))**2,axis=0)
            fy,fx=np.meshgrid(np.fft.fftfreq(height),np.fft.fftfreq(width),indexing='ij')
            radius=np.sqrt(fx*fx+fy*fy)
            low=float(psd[(radius>0)&(radius<.1)].mean())
            high=float(psd[(radius>=.25)&(radius<=.5)].mean())
            split=None
            if count>=8:
                first=values[:count//2,roi,plane].var(axis=0,ddof=1).mean()
                last=values[count//2:,roi,plane].var(axis=0,ddof=1).mean()
                split=float(abs(first-last)/max((first+last)*.5,1e-30))
            rows.append(dict(roi=roi,plane=plane,temporal_variance_dn2=float(variance[roi,plane]),
                shared_spatial_variance_debiased_dn2=float(shared_spatial[roi,plane]),
                mean_dn=float(average[roi,plane].mean()),frame_mean_span_dn=float(np.ptp(drift[:,roi,plane])),
                correlations=correlations,row_mean_power_fraction=row_var/max(power,1e-30),
                column_mean_power_fraction=col_var/max(power,1e-30),
                white_row_fraction=1/width,white_column_fraction=1/height,
                low_high_psd_ratio=low/max(high,1e-30),half_burst_relative_variance_difference=split))
    channels=noise.transpose(2,0,1,3,4).reshape(4,-1)
    covariance=channels@channels.T/channels.shape[1]
    correlation=[[float(covariance[i,j]/np.sqrt(covariance[i,i]*covariance[j,j]))
                  if covariance[i,i]>0 and covariance[j,j]>0 else None for j in range(4)] for i in range(4)]
    return dict(frames=count,shape=list(frames.shape),rows=rows,interplane_correlation=correlation,
                temporal_frame_means_dn=drift.mean(axis=1).tolist(),
                limits=['Shared spatial estimate may contain real dark shading, fixed pattern or residual illumination',
                        'Two frames allow moments but not a split-burst stability check',
                        'Lag and PSD ratios are diagnostics, not a complete independent noise model',
                        'Subtracting per-frame spatial means for correlation removes drift, which is recorded separately'])


def run(root,output):
    from memory_budget import MemoryGuard
    parent=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    data=read(parent)
    groups=defaultdict(list)
    for row in data['frames']:
        if Path(row['path']).parent.name!='blacks':continue
        m=row['metadata']
        groups[(m['model'],m['iso'],m['exposure_s'],m['datetime_original'][:10])].append(row)
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(path=str(parent),sha256=digest(parent)),groups=[],minimum_split_burst_frames=8,
        policy='Use all frozen cached dark frames, chronological per ISO/exposure/day; no selection by noise score',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('physical_dark_audit.py','memory_budget.py','prepare_next_training.py')})
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.8)
    try:
        with guard:
            for key,rows in sorted(groups.items()):
                guard.check()
                rows.sort(key=lambda r:r['metadata']['datetime_original'])
                tiles=[]
                for frame in rows:
                    path=root/frame['cached_tiles']
                    if digest(path)!=frame['tiles_sha256']:raise ValueError('Frozen cached DN changed')
                    if digest(Path(frame['path']))!=frame['source_sha256']:raise ValueError('Original dark RAW changed')
                    tiles.append(np.load(path,allow_pickle=False))
                values=statistics(np.stack(tiles))
                values.update(model=key[0],iso=key[1],exposure_s=key[2],capture_day=key[3],
                    sources=[dict(path=r['path'],sha256=r['source_sha256']) for r in rows],
                    split_repeatability_available=len(rows)>=8,lens_cap_and_temperature_not_proven_by_EXIF=True)
                result['groups'].append(values)
                write(output/'progress.json',result)
            result['status']='completed_physical_diagnostics_not_full_noise_qualification'
    except Exception as error:
        result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-dark-audit-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
