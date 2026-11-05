"""Exploratory sensor-DN law from existing calibration pairs, with frozen gates."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import time

import numpy as np
import rawpy
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from qualify_raw_series import grid_rois,extract_rois
from sensor_noise_math import calibration_pairs,pair_statistics,fit_signal_variance


def save(path,value):
    with path.open('x',encoding='utf-8') as stream:json.dump(value,stream,indent=2,allow_nan=False)


def native_bridge(build,guard):
    """Round-trip native unclipped normalized DN to integers; compare rawpy unpack."""
    records=[]
    for name in ('6V1A0181','6V1A0263','6V1A9819'):
        guard.check()
        descriptor=build/'photo-comparison-v001/full-cfa-sensor-v002'/(name+'.json')
        proof=json.loads(descriptor.read_text(encoding='utf-8'))
        source=Path(proof['source']);binary=descriptor.parent/proof['pixels']
        if sha256(source)!=proof['source_sha256'] or sha256(binary)!=proof['pixels_sha256']:
            raise ValueError('Native bridge source changed')
        checks=[]
        with rawpy.imread(str(source)) as raw,binary.open('rb') as stream:
            view=raw.raw_image_visible
            bits=proof['orientation_flip']
            if bits&2:view=view[::-1]
            if bits&1:view=view[:,::-1]
            if bits&4:view=view.T
            for tile in proof['stages']:
                if tile['stage']!='normalized_cfa_before_wb_unclipped':continue
                stream.seek(tile['offset_bytes'])
                normalized=np.frombuffer(stream.read(384*384*4),dtype='<f4').reshape(384,384)
                black=np.tile(np.asarray(proof['black'])[tile['pattern']],(192,192))
                recovered=np.rint(normalized.astype(np.float64)*proof['divisor']+black)
                x,y=tile['origin_xy'];actual=view[y:y+384,x:x+384]
                delta=float(np.max(np.abs(actual.astype(np.float64)-recovered)))
                checks.append(dict(roi=tile['roi'],max_error_dn=delta,integer_identical=bool(np.array_equal(actual,recovered))))
        if len(checks)!=9 or not all(c['integer_identical'] for c in checks):
            raise ValueError('rawpy unpack differs from native DN bridge')
        records.append(dict(scene=name,native_capture_sha256=sha256(descriptor),source_sha256=proof['source_sha256'],checks=checks))
    return dict(scope='27 known photo ROIs only; integer DN round-trip, not all-files decoder qualification.',records=records)


def fit_all(rows,saturation_key='at_reported_white_fraction'):
    fits=[]
    for iso in sorted({r['iso'] for r in rows}):
        for plane in range(4):
            dark=[r for r in rows if r['iso']==iso and r['plane']==plane and r['kind']=='dark' and r['exposure_s']==.005]
            if not dark:
                fits.append(dict(iso=iso,plane=plane,status='no_reference_dark'));continue
            black=float(np.median([r['mean_dn'] for r in dark]))
            read=float(np.median([r['temporal_variance_dn2'] for r in dark]))
            light=[r for r in rows if r['iso']==iso and r['plane']==plane and r['kind']=='light']
            levels=sorted({r['exposure_s'] for r in light})
            points=[]
            for row in light:
                signal=row['mean_dn']-black
                reasons=[]
                if signal<=0:reasons.append('nonpositive_signal')
                else:
                    if abs(row['mean_drift_dn'])/signal>.01:reasons.append('mean_drift_above_1percent')
                    if row['block_span_dn']/signal>.05:reasons.append('block_span_above_5percent')
                if row[saturation_key]>0:reasons.append('at_unvalidated_reported_white' if saturation_key=='at_reported_white_fraction' else 'at_or_above_saturation_guard')
                if row['temporal_variance_dn2']<=0:reasons.append('nonpositive_variance')
                row['fit_gate']=dict(eligible=not reasons,reasons=reasons,empirical_black_dn=black,
                    signal_dn=signal,split='training' if levels.index(row['exposure_s'])%2==0 else 'heldout')
                if not reasons:
                    points.append(dict(pair=row['pair'],roi=row['roi'],signal_dn=signal,
                        variance_dn2=row['temporal_variance_dn2'],split=row['fit_gate']['split']))
            training=[p for p in points if p['split']=='training'];held=[p for p in points if p['split']=='heldout']
            enough=len({rows[p['pair']*36]['exposure_s'] for p in training})>=2 if training else False
            fit=fit_signal_variance(training,held,read) if enough else dict(status='insufficient_distinct_training_exposures')
            fits.append(dict(iso=iso,plane=plane,empirical_black_dn=black,reference_dark_variance_dn2=read,
                reference_dark_points=len(dark),light_points=len(light),eligible_points=len(points),
                fit=fit,limits='Pooled sessions/temperature unqualified; dark at1/200s is an assumed read intercept for other exposures. Native white not used; rawpy white only flags obvious saturation. No calibrated coefficient accepted.'))
    return fits


def run(output):
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(build.resolve()):raise ValueError('Dedicated output required')
    inventory_path=root/'docs/validation/raw/neural-calibration-inputs-v001-20261105T171100Z.json'
    inventory=json.loads(inventory_path.read_text(encoding='utf-8'))
    pairs,unused=calibration_pairs(inventory['records'])
    if not pairs or len(pairs)>150:raise ValueError('Unexpected pair extent')
    output.mkdir(exist_ok=False,parents=True)
    plan=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),
        inventory_sha256=sha256(inventory_path),pairs=pairs,unpaired=unused,
        tools={p:sha256(Path(__file__).with_name(p)) for p in ('study_sensor_noise.py','sensor_noise_math.py','qualify_raw_series.py')},
        protocol=dict(units='Unprocessed visible CFA integer DN',grid='3x3 centers20/50/80%,128x128 per Bayer site',
            pair_variance='sample_variance(A-B)/2; per-ROI mean drift recorded separately',
            fit_gates=dict(max_relative_mean_drift=.01,max_relative_block_span=.05,any_reported_white_reject=True),
            split='Alternating distinct exposure levels sorted numerically within ISO; no post-score split change.',
            fit='Nonnegative shot slope, pooled dark1/200intercept; experimental, never published as camera calibration',
            no_registration=True,no_black_subtraction_at_capture=True,no_white_balance=True,no_demosaic=True,
            reserve='Only protocole/blacks and protocole/iso-*; rafale and neural-validation unopened'))
    save(output/'plan.json',plan)
    result=dict(schema_version=1,status='started',started_at_utc=datetime.now(timezone.utc).isoformat(),
        plan=plan,plan_sha256=sha256(output/'plan.json'),frames=[],pairs=[],measurements=[],
        versions=dict(numpy=np.__version__,rawpy=rawpy.__version__,libraw=list(rawpy.libraw_version)),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        limits=['Calibration candidates, not new training data or clean targets.',
            'Difference variance suppresses shared fixed structure; not total single-photo noise.',
            'Light drift, spatial texture, temperature and signal dependence may still bias the fit.',
            'Sensor-DN coefficients are not balanced-camera sigma; no automatic change to model inputs.',
            'rawpy LibRaw0.22.1 differs from native0.22.2; 27knownROI integer bridge is limited coverage.'])
    guard=MemoryGuard(hard_bytes=2*1024**3,soft_bytes=1280*1024**2,system_fraction=.92)
    started=time.monotonic()
    try:
        with guard:
            result['native_dn_bridge']=native_bridge(build,guard)
            for number,pair in enumerate(pairs):
                tiles=[];geometry=None;white=[];frame_ids=[]
                for row in pair['frames']:
                    guard.check();path=Path(row['path']);before=path.stat()
                    if (before.st_size,before.st_mtime_ns)!=(row['bytes'],row['mtime_ns']):raise ValueError('Source changed since inventory')
                    digest=sha256(path)
                    with rawpy.imread(str(path)) as raw:
                        mosaic=raw.raw_image_visible;shape=list(mosaic.shape);pattern=raw.raw_pattern.tolist()
                        if geometry is None:geometry=dict(shape=shape,pattern=pattern,rois=grid_rois(shape))
                        if shape!=geometry['shape'] or pattern!=geometry['pattern']:raise ValueError('Pair CFA geometry mismatch')
                        tile=extract_rois(mosaic,geometry['rois']);tiles.append(tile)
                        white.append((tile>=raw.white_level).mean(axis=(-2,-1)))
                        cache=output/(path.stem+'-'+digest[:12]+'.npy')
                        with cache.open('xb') as stream:np.save(stream,tile,allow_pickle=False)
                        frame_ids.append(len(result['frames']))
                        result['frames'].append(dict(path=str(path),source_sha256=digest,metadata=row['metadata'],
                            rawpy_reported_black_unvalidated=raw.black_level_per_channel,rawpy_reported_white_unvalidated=int(raw.white_level),
                            cached_tiles=str(cache),tiles_sha256=sha256(cache),**geometry))
                    after=path.stat()
                    if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):raise ValueError('RAW changed during capture')
                stats=pair_statistics(*tiles)
                result['pairs'].append(dict(index=number,kind=pair['kind'],iso=pair['iso'],exposure_s=pair['exposure_s'],frames=frame_ids))
                for roi in range(9):
                    for plane in range(4):
                        result['measurements'].append(dict(pair=number,kind=pair['kind'],iso=pair['iso'],exposure_s=pair['exposure_s'],
                            roi=roi,plane=plane,colour_index=geometry['pattern'][plane//2][plane%2],
                            at_reported_white_fraction=float(max(w[roi,plane] for w in white)),
                            **{key:float(value[roi,plane]) for key,value in stats.items()}))
                save(output/f'pair-{number:03d}.json',dict(pair=result['pairs'][-1],measurements=result['measurements'][-36:]))
                print('PAIR',number+1,'/',len(pairs),pair['kind'],'ISO',pair['iso'],flush=True)
            result['fits']=fit_all(result['measurements'])
            result['status']='completed_exploratory_sensor_dn_law_not_calibration'
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(elapsed_seconds=time.monotonic()-started,memory=guard.stats())
        save(output/'result.json',result)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        save(root/'docs/validation/raw'/f'neural-sensor-noise-v001-{stamp}.json',result)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    run(parser.parse_args().output)
