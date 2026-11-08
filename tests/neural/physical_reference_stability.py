"""Raw-CFA check of the existing two ISO100 control bursts, no RGB rejection reused."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np
from prepare_next_training import digest,read,write


def compare_bursts(first,second):
    if first.ndim!=5 or second.shape[1:]!=first.shape[1:] or min(len(first),len(second))<2:
        raise ValueError('Two compatible T,ROI,4,H,W bursts required')
    if not np.isfinite(first).all() or not np.isfinite(second).all():raise ValueError('Finite bursts required')
    a,b=first.astype(np.float64),second.astype(np.float64)
    average_a,average_b=a.mean(axis=0),b.mean(axis=0)
    expected=a.var(axis=0,ddof=1)/len(a)+b.var(axis=0,ddof=1)/len(b)
    difference=average_b-average_a
    rows=[]
    for roi in range(first.shape[1]):
        for plane in range(4):
            mse=float(np.mean(difference[roi,plane]**2))
            variance=float(expected[roi,plane].mean())
            rows.append(dict(roi=roi,plane=plane,mean_A_dn=float(average_a[roi,plane].mean()),
                mean_B_dn=float(average_b[roi,plane].mean()),mean_difference_dn=float(difference[roi,plane].mean()),
                mean_difference_mse_dn2=mse,expected_independent_mean_noise_dn2=variance,
                mse_to_expected_ratio=mse/variance if variance>0 else None,
                excess_difference_dn2=mse-variance))
    return rows


def run(root,output):
    import cv2
    import rawpy
    from memory_budget import MemoryGuard
    from qualify_raw_series import grid_rois,extract_rois
    from audit_moon import metadata
    from canon_capture_mode import read_mode
    cv2.setNumThreads(1)
    control=root/'build-patch_neural_restoration/control-pairs-0301-0366-v001/result.json'
    old=read(control)
    by_name={Path(c['source']).stem:c for c in old['captures']}
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(path=str(control),sha256=digest(control)),sources=[],groups=[],
        protocol='Primary301..309, secondary320..327; no reference swap, pooled truth, registration or intensity correction',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('physical_reference_stability.py','qualify_raw_series.py','canon_capture_mode.py')},
        limits=['Raw-CFA temporal check, not a claim of clean reference or independent final test',
                'Drift, moving edges, sensor fixed pattern and noise are not separated by this statistic',
                'Expected mean noise assumes temporal independence; no conclusion from RGB admission reused'])
    arrays=[]
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.8)
    try:
        with guard:
            for name,numbers in (('primary',range(301,310)),('secondary',range(320,328))):
                values=[]
                for number in numbers:
                    guard.check()
                    capture=by_name[f'6V1A{number:04d}']
                    path=Path(capture['source'])
                    if digest(path)!=capture['source_sha256']:raise ValueError('Control RAW identity differs')
                    meta=metadata(path)['metadata']
                    if meta['iso']!=100 or meta['exposure_s']!=.8:raise ValueError('ISO/exposure differs from frozen groups')
                    with rawpy.imread(str(path)) as raw:
                        mosaic=raw.raw_image_visible
                        rois=grid_rois(mosaic.shape)
                        values.append(extract_rois(mosaic,rois))
                        del mosaic
                    result['sources'].append(dict(path=str(path),sha256=capture['source_sha256'],group=name,
                        metadata=meta,mode=read_mode(path)))
                array=np.stack(values)
                arrays.append(array)
                result['groups'].append(dict(name=name,count=len(array),shape=list(array.shape),rois=rois))
            result['comparison']=compare_bursts(*arrays)
            result['within_primary']=compare_bursts(arrays[0][:4],arrays[0][4:])
            result['within_secondary']=compare_bursts(arrays[1][:4],arrays[1][4:])
            # Diagnostic only: do not interpolate/resample CFA or change the reference.
            phase=[]
            for roi in range(arrays[0].shape[1]):
                a=arrays[0].mean(axis=0)[roi,1].astype(np.float64)
                b=arrays[1].mean(axis=0)[roi,1].astype(np.float64)
                a-=a.mean();b-=b.mean()
                window=np.outer(np.hanning(a.shape[0]),np.hanning(a.shape[1]))
                shift,response=cv2.phaseCorrelate(a,b,window)
                phase.append(dict(roi=roi,green_plane=1,shift_packed_xy=list(shift),response=response,
                    raw_pixel_shift_xy=[2*v for v in shift],applied=False))
            result['translation_diagnostic']=phase
            result['additional_protocol']='Fixed first4/last5 primary and first4/last4 secondary splits; phase diagnostic never applied to targets'
            result['status']='completed_raw_reference_diagnostics_not_clean_truth_qualification'
    except Exception as error:
        result.update(status='stopped',error=str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-physical-reference-stability-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.output)
