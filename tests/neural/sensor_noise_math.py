"""Sensor-DN diagnostics; no demosaic, black clipping or learned threshold."""
from datetime import datetime
from pathlib import Path
import re

import numpy as np


def calibration_pairs(records):
    """Disjoint consecutive captures, same EXIF/settings/day/folder, <=30 seconds."""
    eligible=[]
    for row in records:
        path=Path(row['path']);m=row['metadata']
        if 'protocole' not in path.parts or not (path.parent.name=='blacks' or path.parent.name.startswith('iso-')):
            continue
        stamp=datetime.strptime(m['datetime_original'],'%Y:%m:%d %H:%M:%S')
        match=re.fullmatch(r'6V1A(\d{4})',path.stem)
        if not match:raise ValueError('Unexpected calibration filename')
        key=(str(path.parent),m['model'],m['iso'],m['exposure_s'],m['aperture'],m['focal_mm'],stamp.date())
        eligible.append((key,stamp,int(match[1]),row))
    eligible.sort(key=lambda r:(r[0],r[1],r[2]))
    pairs=[];unused=[];pending=None
    for entry in eligible:
        if pending is not None:
            gap=(entry[1]-pending[1]).total_seconds()
            if entry[0]==pending[0] and 0<=gap<=30 and entry[2]==(pending[2]+1)%10000:
                pairs.append(dict(kind='dark' if Path(entry[3]['path']).parent.name=='blacks' else 'light',
                    iso=entry[3]['metadata']['iso'],exposure_s=entry[3]['metadata']['exposure_s'],
                    frames=[pending[3],entry[3]]));pending=None;continue
            unused.append(pending[3])
        pending=entry
    if pending is not None:unused.append(pending[3])
    return pairs,unused


def pair_statistics(first,second):
    """R,C,H,W planes, sample variance(diff)/2; mean drift is separate."""
    if (first.shape!=second.shape or first.ndim!=4 or min(first.shape[-2:])<16
            or any(v%16 for v in first.shape[-2:]) or not np.isfinite(first).all() or not np.isfinite(second).all()):
        raise ValueError('Matching finite Bayer-plane tiles divisible by16 required')
    a=first.astype(np.float64);b=second.astype(np.float64)
    mean=(a+b)*.5;difference=a-b
    h,w=mean.shape[-2:]
    blocks=mean.reshape(*mean.shape[:2],h//16,16,w//16,16).mean(axis=(-3,-1))
    return dict(mean_dn=mean.mean(axis=(-2,-1)),mean_drift_dn=difference.mean(axis=(-2,-1)),
        temporal_variance_dn2=difference.var(axis=(-2,-1),ddof=1)/2,
        difference_mse_over2_dn2=(difference**2).mean(axis=(-2,-1))/2,
        spatial_variance_dn2=(a.var(axis=(-2,-1),ddof=1)+b.var(axis=(-2,-1),ddof=1))*.5,
        block_span_dn=np.quantile(blocks,.9,axis=(-2,-1))-np.quantile(blocks,.1,axis=(-2,-1)))


def fit_signal_variance(training,heldout,read_variance):
    """Fixed dark intercept and nonnegative shot slope; descriptive holdout only."""
    if not np.isfinite(read_variance) or read_variance<0:raise ValueError('Invalid dark variance')
    if len(training)<3 or not heldout:return dict(status='insufficient_eligible_training_or_holdout')
    values=np.asarray([(r['signal_dn'],r['variance_dn2']) for r in training],dtype=np.float64)
    if not np.isfinite(values).all() or (values<=0).any():raise ValueError('Positive finite signal/variance required')
    x,y=values.T
    if x.max()/x.min()<2:return dict(status='insufficient_signal_span')
    slope=max(0.,float(np.dot(x,y-read_variance)/np.dot(x,x)))
    constant=float(np.median(y));rows=[]
    for row in heldout:
        signal,variance=row['signal_dn'],row['variance_dn2']
        if not np.isfinite([signal,variance]).all() or min(signal,variance)<=0:raise ValueError('Invalid holdout')
        prediction=slope*signal+read_variance
        rows.append(dict(**row,predicted_variance_dn2=prediction,constant_variance_dn2=constant,
            relative_variance_error=abs(prediction/variance-1),constant_relative_variance_error=abs(constant/variance-1),
            inside_training_signal_span=bool(x.min()<=signal<=x.max())))
    return dict(status='descriptive_fit_not_calibration',shot_slope_dn=slope,read_variance_dn2=read_variance,
        training_signal_span_dn=[float(x.min()),float(x.max())],training_points=len(training),heldout=rows,
        median_relative_variance_error=float(np.median([r['relative_variance_error'] for r in rows])),
        median_constant_relative_variance_error=float(np.median([r['constant_relative_variance_error'] for r in rows])))


def near_white_fraction(tiles,reported_white,headroom=16):
    """Conservative code-space guard, not a measured physical sensor white."""
    if not np.isfinite(reported_white) or reported_white<=headroom or headroom<0 or not np.isfinite(tiles).all():
        raise ValueError('Finite pixels and positive white/headroom required')
    return (tiles>=reported_white-headroom).mean(axis=(-2,-1))
