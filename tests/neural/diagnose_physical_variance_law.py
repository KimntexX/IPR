"""Training-only exposure-level cross-validation of linear/quadratic variance.

No coefficients are selected on held-out physical controls. No model is promoted.
"""
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import numpy as np
from fit_photon_covariance import dark_sources
from prepare_next_training import digest


def fit(records, degree):
    if degree not in (1,2) or len(records)<degree+1:
        raise ValueError('Enough records and a linear or quadratic law required')
    data=np.asarray([(r['signal'],r['variance'],r['read']) for r in records])
    if not np.isfinite(data).all() or (data[:,:2]<=0).any() or (data[:,2]<0).any():
        raise ValueError('Finite physical signal, variance and read intercept required')
    signal,variance,read=data.T
    scale=signal.max()
    cluster_counts={r['pair']:sum(s['pair']==r['pair'] for s in records) for r in records}
    weights=np.asarray([1/np.sqrt(cluster_counts[r['pair']]) for r in records])/variance
    design=np.stack([(signal/scale)**power for power in range(1,degree+1)],axis=1)
    coefficients,_,rank,_=np.linalg.lstsq(design*weights[:,None],(variance-read)*weights,rcond=None)
    if rank!=degree:
        raise ValueError('Variance law is not identifiable')
    a=float(coefficients[0]/scale)
    c=float(coefficients[1]/scale**2) if degree==2 else 0.
    return dict(a=a,c=c,measured_signal_span_dn=[float(signal.min()),float(signal.max())],
                positive_photon_gain_in_span=bool(min(a,a+c*scale)>0))


def cross_validate(records, degree):
    rows=[]
    for exposure in sorted({r['exposure_s'] for r in records}):
        train=[r for r in records if r['exposure_s']!=exposure]
        control=[r for r in records if r['exposure_s']==exposure]
        law=fit(train,degree)
        errors=[]
        for row in control:
            prediction=row['read']+law['a']*row['signal']+law['c']*row['signal']**2
            errors.append(abs(prediction/row['variance']-1))
        rows.append(dict(exposure_s=exposure,median_relative_error=float(np.median(errors)),
                         positive_gain=law['positive_photon_gain_in_span'],training_law=law))
    return dict(median_exposure_error=float(np.median([r['median_relative_error'] for r in rows])),folds=rows)


def run(root):
    bank,sources=dark_sources(root)
    now=datetime.now(timezone.utc)
    result=dict(schema_version=1,data_version='v001',campaign_id='physical-variance-law-diagnostic-'+now.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=now.isoformat(),status='training_only_variance_law_diagnostic_not_qualified',groups=[],
        software=dict(python=platform.python_version(),numpy=np.__version__),tool_sha256=digest(Path(__file__)),
        method='Equal pair weights, relative least squares, read intercept per ROI from calibration darks; leave one entire training exposure out',
        limits=['Exploratory diagnostic; not a fresh validation or simulator change',
            'Quadratic describes conditional variance, not an identified sensor transfer function',
            'Extreme-exposure folds may extrapolate; predictions are diagnostic only',
            'No heldout exposure is read or used for fitting or selection'])
    pairs={r['index']:r for r in bank['pairs']}
    for iso,source in sorted(sources.items()):
        read_variance=np.var(source.astype(np.float64),axis=0,ddof=1).mean(axis=(-2,-1))
        for plane in range(4):
            rows=[dict(pair=r['pair'],exposure_s=pairs[r['pair']]['exposure_s'],signal=r['fit_gate']['signal_dn'],
                       variance=r['temporal_variance_dn2'],read=float(read_variance[r['roi'],plane]))
                for r in bank['measurements'] if r['iso']==iso and r['plane']==plane
                and r.get('fit_gate',{}).get('eligible') and r['fit_gate']['split']=='training']
            result['groups'].append(dict(iso=iso,plane=plane,training_records=rows,
                linear=dict(law=fit(rows,1),cv=cross_validate(rows,1)),
                quadratic=dict(law=fit(rows,2),cv=cross_validate(rows,2))))
    path=root/'docs/validation/raw'/f"neural-physical-variance-law-diagnostic-v001-{now.strftime('%Y%m%dT%H%M%SZ')}.json"
    with path.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,allow_nan=False,separators=(',',':'))
    print(path.name)
    for row in result['groups']:
        print(row['iso'],row['plane'], 'linear',round(row['linear']['cv']['median_exposure_error'],4),
            'quadratic',round(row['quadratic']['cv']['median_exposure_error'],4),
            'positive',row['quadratic']['law']['positive_photon_gain_in_span'])


if __name__=='__main__':
    run(Path(__file__).resolve().parents[2])
