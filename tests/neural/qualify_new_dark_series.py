"""Chronologically split new dark acquisitions before measuring their noise.

This qualifies a component of the physical bench, never the whole benchmark.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import numpy as np

from prepare_next_training import digest,read,write
from physical_dark_audit import statistics


def split_sources(records,iso,day):
    rows=[r for r in records if r['metadata'].get('iso')==iso
          and r['metadata'].get('datetime_original','').startswith(day)
          and r['metadata'].get('exposure_s')==.005 and r['capture_mode']['mode']=='RAW']
    rows.sort(key=lambda r:(r['metadata']['datetime_original'],r['path']))
    if len(rows)<48:raise ValueError('32 calibration and at least16 control frames required')
    return rows[:32],rows[32:]


def compare_groups(calibration,control):
    if calibration.ndim!=5 or calibration.shape[1:]!=control.shape[1:] or len(calibration)<2 or len(control)<2:
        raise ValueError('Two compatible T,ROI,4,H,W dark groups required')
    rows=[]
    correlations=[]
    for roi in range(calibration.shape[1]):
        # One ROI at a time bounds float64 workspaces for large dark bursts.
        a=statistics(calibration[:,roi:roi+1])
        b=statistics(control[:,roi:roi+1])
        mean_a=calibration[:,roi].astype(np.float64).mean(axis=0)
        mean_b=control[:,roi].astype(np.float64).mean(axis=0)
        for plane in range(4):
            x,y=a['rows'][plane],b['rows'][plane]
            va,vb=x['temporal_variance_dn2'],y['temporal_variance_dn2']
            ca=mean_a[plane]-mean_a[plane].mean()
            cb=mean_b[plane]-mean_b[plane].mean()
            rows.append(dict(roi=roi,plane=plane,calibration=x,control=y,
                relative_variance_difference=abs(va-vb)/max((va+vb)*.5,1e-30),
                relative_psd_band_ratio_difference=abs(x['low_high_psd_ratio']-y['low_high_psd_ratio'])/max((x['low_high_psd_ratio']+y['low_high_psd_ratio'])*.5,1e-30),
                largest_abs_lag_difference=max(abs(x['correlations'][k]-y['correlations'][k]) for k in x['correlations']),
                common_pattern_cross_covariance_dn2=float(np.mean(ca*cb)),
                absolute_mean_drift_dn=abs(x['mean_dn']-y['mean_dn'])))
        correlations.append(dict(roi=roi,calibration=a['interplane_correlation'],control=b['interplane_correlation']))
    return dict(rows=rows,interplane=correlations)


def assess(rows):
    criteria=dict(variance_median=.05,variance_q95=.10,psd_ratio_median=.10,psd_ratio_q95=.20,
                  lag_difference_median=.02,lag_difference_q95=.05)
    values={}
    for key,field in (('variance','relative_variance_difference'),('psd_ratio','relative_psd_band_ratio_difference'),
                      ('lag_difference','largest_abs_lag_difference')):
        sample=[r[field] for r in rows]
        values[key+'_median']=float(np.median(sample));values[key+'_q95']=float(np.quantile(sample,.95))
    return dict(criteria=criteria,values=values,passed=all(values[k]<=v for k,v in criteria.items()),
                scope='Engineering repeatability guards for the dark component, not photographic acceptance')


def run(root,inventory,output,day):
    import rawpy
    from qualify_raw_series import grid_rois,extract_rois
    from memory_budget import MemoryGuard
    records=read(inventory)['records']
    groups={iso:split_sources(records,iso,day) for iso in (100,16000)}
    output.mkdir(exist_ok=False)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        parent=dict(path=str(inventory),sha256=digest(inventory)),capture_day=day,groups=[],
        protocol=dict(split='First32 calibration, chronological remainder control; no data exclusion by score',
                      exposure_s=.005,capture_mode='RAW',no_black_subtraction_or_clipping=True),
        tools={n:digest(Path(__file__).with_name(n)) for n in ('qualify_new_dark_series.py','physical_dark_audit.py','qualify_raw_series.py','memory_budget.py')},
        limits=['Same-session control is not proof for all temperatures/readout modes',
                'Nine fixed ROI do not qualify full-frame hot pixels or all fixed-pattern structure',
                'Readout statistics include ADC/quantization and decoder effects, not isolated analog read noise',
                'Passing dark repeatability is not full physical qualification or permission to train'])
    # Record all criteria and the source split before unpacking any new frame.
    result['criteria_before_read']=assess([dict(relative_variance_difference=0,
        relative_psd_band_ratio_difference=0,largest_abs_lag_difference=0)])['criteria']
    result['declared_sources']={str(iso):dict(calibration=[r['path'] for r in pair[0]],control=[r['path'] for r in pair[1]])
                                for iso,pair in groups.items()}
    write(output/'freeze.json',result)
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    try:
        with guard:
            for iso,(calibration,control) in groups.items():
                arrays=[];identities=[];hashes=set()
                for row in calibration+control:
                    guard.check()
                    source=Path(row['path']);identity=digest(source)
                    if identity in hashes:raise ValueError('Duplicate RAW cannot count as an independent capture')
                    hashes.add(identity)
                    before=source.stat()
                    with rawpy.imread(str(source)) as raw:
                        mosaic=raw.raw_image_visible
                        rois=grid_rois(mosaic.shape)
                        arrays.append(extract_rois(mosaic,rois))
                        pattern=raw.raw_colors_visible[:2,:2].tolist()
                        del mosaic
                    after=source.stat()
                    if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):raise ValueError('Source changed during read')
                    identities.append(dict(path=str(source),sha256=identity,metadata=row['metadata'],pattern=pattern,
                        role='calibration' if len(identities)<32 else 'control'))
                pixels=np.stack(arrays);del arrays
                cache=output/f'iso{iso}-dn.npy';np.save(cache,pixels,allow_pickle=False)
                comparison=compare_groups(pixels[:32],pixels[32:])
                row=dict(iso=iso,calibration_frames=32,control_frames=len(pixels)-32,source_identities=identities,
                    cached_dn=dict(path=str(cache),sha256=digest(cache),shape=list(pixels.shape),dtype=str(pixels.dtype)),
                    rois=rois,comparison=comparison,assessment=assess(comparison['rows']))
                result['groups'].append(row);del pixels
                write(output/'progress.json',result)
            passed=all(r['assessment']['passed'] for r in result['groups'])
            result['status']='passed_dark_repeatability_component_not_full_bench' if passed else 'dark_repeatability_guard_not_met'
    except Exception as error:
        result.update(status='stopped',error=type(error).__name__+': '+str(error));raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats())
        write(output/'progress.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-new-dark-qualification-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inventory',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--day',required=True)
    a=p.parse_args();run(Path(__file__).resolve().parents[2],a.inventory,a.output,a.day)
