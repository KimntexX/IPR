"""Capture-pair bootstrap of training-only photon slopes; no control refit."""
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import numpy as np
from prepare_next_training import digest, read


def cluster_slopes(records, read_variance, seed, repetitions=2000):
    if not records or read_variance < 0 or not np.isfinite(read_variance):
        raise ValueError('Finite training records and read variance required')
    ids=sorted({r['pair'] for r in records})
    if len(ids) < 2:
        raise ValueError('At least two distinct calibration pairs required')
    values=np.asarray([(r['signal_dn'],r['variance_dn2']) for r in records])
    if not np.isfinite(values).all() or (values<=0).any():
        raise ValueError('Positive finite training signal and variance required')
    numerator=[];denominator=[]
    for pair in ids:
        group=np.asarray([(r['signal_dn'],r['variance_dn2']) for r in records if r['pair']==pair])
        numerator.append(float(np.sum(group[:,0]*(group[:,1]-read_variance))))
        denominator.append(float(np.sum(group[:,0]**2)))
    numerator=np.asarray(numerator);denominator=np.asarray(denominator)
    rng=np.random.default_rng(seed);draws=rng.integers(len(ids),size=(repetitions,len(ids)))
    slopes=numerator[draws].sum(axis=1)/denominator[draws].sum(axis=1)
    return dict(pairs=ids,fields=len(records),bootstrap_repetitions=repetitions,
        point_slope_dn=float(numerator.sum()/denominator.sum()),
        descriptive_percentile95=np.quantile(slopes,[.025,.975]).tolist(),
        pair_slopes_dn=(numerator/denominator).tolist())


def run(root):
    path=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    bank=read(path)
    now=datetime.now(timezone.utc)
    result=dict(schema_version=1,data_version='v001',campaign_id='physical-parameter-uncertainty-'+now.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=now.isoformat(),status='descriptive_training_cluster_uncertainty_not_qualification',
        provenance=dict(bank_path=str(path),bank_sha256=digest(path)),
        software=dict(python=platform.python_version(),numpy=np.__version__),
        tool_sha256=digest(Path(__file__)),groups=[],
        limits=['Training-only pairs; no test-set coefficient adjustment',
            'Percentile bootstrap is descriptive with few exposure clusters, not certified coverage',
            'Assumes the historical fixed read intercept; covariance-parameter uncertainty not evaluated'])
    for fit in bank['fits']:
        iso,plane=fit['iso'],fit['plane']
        if iso not in (100,3200,6400,16000,51200):
            continue
        rows=[dict(pair=r['pair'],signal_dn=r['fit_gate']['signal_dn'],variance_dn2=r['temporal_variance_dn2'])
            for r in bank['measurements'] if r['iso']==iso and r['plane']==plane
            and r.get('fit_gate',{}).get('eligible') and r['fit_gate']['split']=='training']
        result['groups'].append(dict(iso=iso,plane=plane,**cluster_slopes(rows,fit['reference_dark_variance_dn2'],
            841000+iso+plane),original_slope_dn=fit['fit']['shot_slope_dn']))
    output=root/'docs/validation/raw'/f"neural-physical-parameter-uncertainty-v001-{now.strftime('%Y%m%dT%H%M%SZ')}.json"
    with output.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,allow_nan=False,separators=(',',':'))
    print(output.name)
    for group in result['groups']:
        if group['plane']==0:
            print(group['iso'],group['point_slope_dn'],group['descriptive_percentile95'],len(group['pairs']))


if __name__=='__main__':
    run(Path(__file__).resolve().parents[2])
