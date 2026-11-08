"""Enlarge the frozen physical grid without choosing regions by spectral scores.

Reads originals, keeps DN and Bayer phase, and rechecks the physical flat gates.
No training, image exports or source deletion.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import platform
import time
import numpy as np
from prepare_next_training import digest, read, write
from sensor_noise_math import pair_statistics


def enlarge(rois, side=256):
    if side < 128 or side % 16:
        raise ValueError('Expanded side >=128 and divisible by16 required')
    return [dict(x=r['x']+r['width']//2-side//2,y=r['y']+r['height']//2-side//2,
                 width=side,height=side) for r in rois]


def physical_gate(mean_dn, drift_dn, span_dn, maximum_dn, black_dn):
    if not np.isfinite([mean_dn,drift_dn,span_dn,maximum_dn,black_dn]).all():
        raise ValueError('Finite physical moments required')
    signal=mean_dn-black_dn
    reasons=[]
    if signal <= 0:
        reasons.append('nonpositive_signal')
    else:
        if abs(drift_dn) > .01*signal:
            reasons.append('mean_drift_above_1percent')
        if span_dn > .05*signal:
            reasons.append('block_span_above_5percent')
    # Below the native white levels measured on this R6 III scope.
    # Conservative constant, not chosen from spectral agreement.
    if maximum_dn >= 12000:
        reasons.append('at_conservative_12000_dn_guard')
    return dict(eligible=not reasons,reasons=reasons,signal_dn=float(signal))


def run(root, output):
    import rawpy
    from qualify_raw_series import grid_rois,extract_rois
    from memory_budget import MemoryGuard
    build=root/'build-patch_neural_restoration'
    bank_path=root/'docs/validation/raw/neural-sensor-noise-v001-20261105T173608Z.json'
    bank=read(bank_path)
    selected_isos=(100,3200,6400,16000,51200)
    source_groups=[]
    for parent in ('new-dark-qualification-v001','remaining-dark-component-v001'):
        for group in read(build/parent/'progress.json')['groups']:
            source_groups.append(dict(iso=group['iso'],sources=[s for s in group['source_identities']
                if s['role']=='calibration']))
    pairs=[p for p in bank['pairs'] if p['kind']=='light' and p['iso'] in selected_isos]
    frame_indices=sorted({i for p in pairs for i in p['frames']})
    output.mkdir(exist_ok=False)
    now=datetime.now(timezone.utc)
    result=dict(schema_version=1,data_version='v001',campaign_id='larger-physical-tiles-'+now.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=now.isoformat(),status='started',sources=[],dark_groups=[],fields=[],
        bank_path=str(bank_path),bank_sha256=digest(bank_path),
        software=dict(python=platform.python_version(),numpy=np.__version__,rawpy=rawpy.__version__,libraw=list(rawpy.libraw_version)),
        tools={name:digest(Path(__file__).with_name(name)) for name in
            ('prepare_larger_physical_tiles.py','sensor_noise_math.py','qualify_raw_series.py','memory_budget.py')},
        rules=dict(side_packed=256,centres='Same centres as original128 grid',
            thresholds=dict(mean_drift=.01,block_span=.05,raw_dn_guard=12000),
            selection='Original eligible fields only, both exposure roles preserved; expanded physical gates only',
            max_wall_seconds=1800),
        limits=['Known controls, not a fresh final reserve','Expanded ROI eligibility is separate from spectral agreement',
            'No optical-black recalibration or correction of the input RAW','Not a complete benchmark qualification'])
    write(output/'freeze.json',{**result,'dark_source_manifest':source_groups,'light_frame_indices':frame_indices})
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1536*1024**2,system_fraction=.8)
    start=time.monotonic()

    def extract(source, expected, label, old_rois=None):
        guard.check()
        if time.monotonic()-start > 1800:
            raise TimeoutError('Larger-tile extraction deadline reached')
        path=Path(source)
        if digest(path)!=expected:
            raise ValueError('Original RAW changed: '+str(path))
        with rawpy.imread(str(path)) as raw:
            mosaic=raw.raw_image_visible
            if list(mosaic.shape)!=bank['frames'][0]['shape'] or raw.raw_pattern.tolist()!=bank['frames'][0]['pattern']:
                raise ValueError('Source geometry or Bayer phase changed')
            rois=enlarge(old_rois if old_rois is not None else grid_rois(mosaic.shape))
            tiles=extract_rois(mosaic,rois)
            geometry=dict(shape=list(mosaic.shape),pattern=raw.raw_pattern.tolist(),
                          visible_origin=[raw.sizes.left_margin,raw.sizes.top_margin])
        cached=output/(label+'.npy')
        with cached.open('xb') as stream:
            np.save(stream,tiles,allow_pickle=False)
        row=dict(source=str(path),source_sha256=expected,path=str(cached.resolve()),sha256=digest(cached),
                 rois=rois,geometry=geometry)
        result['sources'].append(row)
        write(output/'progress.json',dict(status='extracting',completed_sources=len(result['sources']),
              elapsed_seconds=time.monotonic()-start,memory=guard.stats()))
        return row

    try:
        with guard:
            for group in source_groups:
                cached=[extract(s['path'],s['sha256'],f"dark-{group['iso']}-{i:03d}")
                        for i,s in enumerate(group['sources'])]
                result['dark_groups'].append(dict(iso=group['iso'],sources=cached))
            frames={index:extract(bank['frames'][index]['path'],bank['frames'][index]['source_sha256'],
                    f'light-{index:03d}',bank['frames'][index]['rois']) for index in frame_indices}
            result['light_frames']={str(index):row for index,row in frames.items()}
            measurements={(r['pair'],r['roi'],r['plane']):r for r in bank['measurements']}
            for pair in pairs:
                arrays=[np.load(frames[index]['path'],allow_pickle=False) for index in pair['frames']]
                moments=pair_statistics(*arrays)
                maximum=np.maximum(arrays[0].max(axis=(-2,-1)),arrays[1].max(axis=(-2,-1)))
                for roi in range(9):
                    for plane in range(4):
                        old=measurements[(pair['index'],roi,plane)]
                        if not old.get('fit_gate',{}).get('eligible'):
                            continue
                        gate=physical_gate(moments['mean_dn'][roi,plane],moments['mean_drift_dn'][roi,plane],
                            moments['block_span_dn'][roi,plane],maximum[roi,plane],old['fit_gate']['empirical_black_dn'])
                        result['fields'].append(dict(iso=pair['iso'],pair=pair['index'],roi=roi,plane=plane,
                            split=old['fit_gate']['split'],gate=gate,
                            moments={name:float(value[roi,plane]) for name,value in moments.items()}))
            result.update(status='completed_expanded_physical_tiles_not_qualified',
                summary=[dict(iso=iso,split=split,total=sum(r['iso']==iso and r['split']==split for r in result['fields']),
                    eligible=sum(r['iso']==iso and r['split']==split and r['gate']['eligible'] for r in result['fields']))
                    for iso in selected_isos for split in ('training','heldout')])
    except Exception as error:
        result.update(status='stopped',error=str(error))
        raise
    finally:
        result.update(finished_at_utc=datetime.now(timezone.utc).isoformat(),memory=guard.stats(),
                      elapsed_seconds=time.monotonic()-start)
        write(output/'result.json',result)
        write(output/'progress.json',{k:v for k,v in result.items() if k not in ('sources','fields','light_frames','dark_groups')})
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-expanded-physical-tiles-v001-{stamp}.json').open('x',encoding='utf-8') as stream:
            json.dump(result,stream,allow_nan=False,separators=(',',':'))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    run(Path(__file__).resolve().parents[2],args.output)
