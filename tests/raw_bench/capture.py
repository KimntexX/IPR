"""Versioned RAW bench captures and fail-closed stage comparisons (I3)."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path

import numpy as np

DTYPES={'u16':np.dtype('<u2'),'f32':np.dtype('<f4'),'f64':np.dtype('<f8')}
REQUIRED=('unpacked_cfa','pre_demosaic_cfa','post_median_camera_rgb','working_rec2020')


def sha256(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def load_capture(path):
    path=Path(path);manifest=json.loads(path.read_text(encoding='utf-8'))
    if manifest.get('schema_version')!=1 or not manifest.get('stages') or not manifest.get('source',{}).get('sha256'):
        raise ValueError('Missing capture contract or unsupported schema')
    pixels=Path(manifest['pixels']['path'])
    if not pixels.is_absolute():pixels=path.parent/pixels
    if sha256(pixels)!=manifest['pixels']['sha256']:raise ValueError('Capture pixel hash mismatch')
    size=pixels.stat().st_size;stages={};extents=[]
    with pixels.open('rb') as handle:
        for stage in manifest['stages']:
            shape=stage['shape'];dtype=DTYPES[stage['dtype']]
            if len(shape)!=3 or any(type(n)!=int or n<=0 for n in shape):raise ValueError('Invalid stage shape')
            offset=stage['offset_bytes'];count=int(np.prod(shape,dtype=object));nbytes=count*dtype.itemsize
            if type(offset)!=int or offset<0 or offset%dtype.itemsize or offset+nbytes>size:raise ValueError('Invalid stage extent')
            if stage['stride_bytes']!=shape[1]*shape[2]*dtype.itemsize:raise ValueError('Capture must be packed')
            if any(offset<end and offset+nbytes>start for start,end in extents):raise ValueError('Overlapping stage extents')
            extents.append((offset,offset+nbytes))
            key=(stage['stage'],tuple(stage['origin_yx']))
            if key in stages:raise ValueError('Duplicate stage/ROI')
            for required in ('unit','layout','orientation','context'):
                if required not in stage:raise ValueError('Incomplete stage descriptor')
            handle.seek(offset);array=np.frombuffer(handle.read(nbytes),dtype=dtype).reshape(shape)
            stages[key]=(stage,array)
    return manifest,stages


def compare_captures(reference,candidate,required=REQUIRED,tolerances=None,purpose='parity'):
    if purpose not in ('parity','measurement'):raise ValueError('Unknown comparison purpose')
    tolerances=tolerances or {}
    a,aa=load_capture(reference);b,bb=load_capture(candidate)
    if a['source']['sha256']!=b['source']['sha256']:raise ValueError('Different RAW sources')
    if purpose=='parity' and (a['mode']!='parity' or b['mode']!='parity'):raise ValueError('Signed mode cannot certify parity')
    rows=[];missing=[];contract=[]
    for name in required:
        left={k for k in aa if k[0]==name};right={k for k in bb if k[0]==name}
        if not left or not right or left!=right:missing.append(name)
    for key in sorted(set(aa)|set(bb)):
        if key[0] not in required:continue
        if key not in aa or key not in bb:
            missing.append(str(key));continue
        sa,x=aa[key];sb,y=bb[key]
        fields=('shape','dtype','origin_yx','layout','orientation','unit','context')
        mismatch=[f for f in fields if sa[f]!=sb[f]]
        if mismatch:
            contract.append(dict(stage=key[0],origin_yx=list(key[1]),fields=mismatch));continue
        finite=bool(np.isfinite(x).all() and np.isfinite(y).all())
        row=dict(stage=key[0],origin_yx=list(key[1]),finite=finite,bitwise_equal=x.tobytes()==y.tobytes())
        if finite:
            tol=tolerances.get(key[0],{'atol':0.,'rtol':0.});atol=float(tol['atol']);rtol=float(tol['rtol'])
            if not np.isfinite([atol,rtol]).all() or min(atol,rtol)<0:raise ValueError('Invalid tolerances')
            if sa['dtype']=='u16' and (atol or rtol):raise ValueError('Integer boundaries require exact equality')
            delta=x.astype(float)-y.astype(float);absolute=abs(delta)
            row.update(max_abs=float(absolute.max()),mae=float(absolute.mean()),rmse=float(np.sqrt(np.mean(delta**2))),
                unequal_values=int(np.count_nonzero(x!=y)),outside_tolerance=int(np.count_nonzero(absolute>atol+rtol*abs(x.astype(float)))),tolerance=tol,
                reference_negative_fraction=float(np.mean(x<0)),candidate_negative_fraction=float(np.mean(y<0)),
                reference_zero_fraction=float(np.mean(x==0)),candidate_zero_fraction=float(np.mean(y==0)),
                reference_mean=x.mean(axis=(0,1),dtype=float).tolist(),candidate_mean=y.mean(axis=(0,1),dtype=float).tolist(),
                reference_variance=x.var(axis=(0,1),dtype=float).tolist(),candidate_variance=y.var(axis=(0,1),dtype=float).tolist(),
                reference_covariance=np.atleast_2d(np.cov(x.astype(float).reshape(-1,x.shape[-1]),rowvar=False)).tolist(),
                candidate_covariance=np.atleast_2d(np.cov(y.astype(float).reshape(-1,y.shape[-1]),rowvar=False)).tolist())
        rows.append(row)
    valid=not missing and not contract and all(r['finite'] for r in rows)
    passed=valid and all(r['outside_tolerance']==0 for r in rows)
    return dict(schema_version=1,measured_at_utc=datetime.now(timezone.utc).isoformat(),purpose=purpose,
        reference=dict(path=str(reference),sha256=sha256(reference)),candidate=dict(path=str(candidate),sha256=sha256(candidate)),
        required_stages=list(required),uncompared_reference_stages=sorted({k[0] for k in aa if k[0] not in required}),
        uncompared_candidate_stages=sorted({k[0] for k in bb if k[0] not in required}),
        missing=missing,contract_mismatches=contract,stages=rows,
        verdict=('pass' if passed else 'fail') if purpose=='parity' else ('measured_not_parity' if valid else 'invalid_measurement'))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('reference',type=Path);p.add_argument('candidate',type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--require-stage',action='append');p.add_argument('--tolerances',type=Path)
    p.add_argument('--purpose',choices=('parity','measurement'),default='parity');args=p.parse_args()
    result=compare_captures(args.reference,args.candidate,args.require_stage or REQUIRED,
        json.loads(args.tolerances.read_text(encoding='utf-8')) if args.tolerances else None,args.purpose)
    with args.output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(result['verdict'])
    return 0 if result['verdict'] in ('pass','measured_not_parity') else 1


if __name__=='__main__':raise SystemExit(main())
