"""I/O and orchestration for a frozen, exploratory signed-camera photo pilot."""
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import sys

import cv2
import numpy as np

from audit_moon import digest
from photo_pair_math import (balanced_gains,translation,reference_tile,warp,
                             affine_fit,sigma_proxy,tile_quality,patches)


def read_plane(capture,descriptor):
    path=Path(capture['descriptor']).parent/capture['pixels']
    if path.parent!=Path(capture['descriptor']).parent or descriptor['dtype']!='<f4':
        raise ValueError('Invalid capture path/type')
    shape=descriptor['shape'];count=int(np.prod(shape))
    with path.open('rb') as handle:
        handle.seek(descriptor['offset_bytes'])
        data=handle.read(count*4)
    if len(data)!=count*4:
        raise ValueError('Truncated camera data')
    array=np.frombuffer(data,dtype='<f4').reshape(shape).copy()
    if not np.isfinite(array).all():
        raise ValueError('Nonfinite camera data')
    return array


def alignment(reference,capture,protocol):
    shape=capture['full_shape']
    source=read_plane(capture,capture['green_proxy'])[:,:,0]
    return translation(reference,source,(shape[1]/source.shape[1],shape[0]/source.shape[0]),
                       protocol['ecc_floor'],protocol['max_translation_pixels'],
                       protocol['unsaturated_camera_fraction'])


def prepare(work,freeze,captures):
    cv2.setNumThreads(2)
    protocol=freeze['qualification']
    proof=dict(schema_version=1,data_version=1,campaign_id=freeze['campaign_id']+'-pairs',
        measured_at_utc=datetime.now(timezone.utc).isoformat(),freeze_sha256=digest(work/'freeze.json'),
        protocol=protocol,source_hashes={Path(p).name:digest(p) for p in
            (__file__,Path(__file__).with_name('photo_pair_math.py'))},
        versions=dict(python=sys.version,numpy=np.__version__,opencv=cv2.__version__),
        alignment=[],references=[],frames=[],examples=[],
        limits=['Two compositions only; no independent test partition.',
            'Translation-only alignment; local inconsistency may be motion or illumination.',
            'Reference stability and pair gates are provisional, not calibrated quality guarantees.',
            'Single-image Haar MAD conditioning is a texture/correlation-affected proxy, not sensor truth.',
            'No noisy input interpolation; reference interpolation correlates target noise.',
            'Pair admission uses low-frequency source/reference consistency, not learned output or high-pass residual masking.',
            'Global channel gains/offsets fit on five ROIs; require two consistent distinct monitoring ROIs.',
            'Burst means retain fixed-pattern noise and are not perfectly clean targets.'])
    for capture in captures:
        if digest(Path(capture['descriptor']))!=capture['descriptor_sha256']:
            raise ValueError('Changed camera descriptor')
        if digest(Path(capture['descriptor']).parent/capture['pixels'])!=capture['pixels_sha256']:
            raise ValueError('Changed camera pixels')
    all_signals=[];all_targets=[];all_masks=[];all_sigmas=[]
    for scene in ('indoor','vegetation'):
        scene_frames=[c for c in captures if c['scene']==scene]
        reference_frames=[c for c in scene_frames if c['iso']==100]
        base=reference_frames[0]
        green=read_plane(base,base['green_proxy'])[:,:,0]
        accepted=[];shifts=[]
        for capture in reference_frames:
            result=(dict(accepted=True,ecc=1.,shift_xy=[0.,0.],reason='reference_origin')
                    if capture is base else alignment(green,capture,protocol))
            proof['alignment'].append(dict(source=capture['source'],scene=scene,role='reference',**result))
            if result['accepted']:
                accepted.append(capture);shifts.append(result['shift_xy'])
        if len(accepted)<8:
            proof['references'].append(dict(scene=scene,accepted=False,reason='fewer_than_eight_registered_reference_frames'))
            continue
        reference_images=[];reference_masks=[]
        for roi in range(9):
            arrays=[read_plane(c,c['tiles'][roi]) for c in accepted]
            gains=[balanced_gains(c['camera_neutral']) for c in accepted]
            mean,valid,summary=reference_tile(arrays,shifts,gains,protocol)
            reference_images.append(mean);reference_masks.append(valid)
            proof['references'].append(dict(scene=scene,roi=roi,**summary))
        for capture in [c for c in scene_frames if c['iso']!=100]:
            result=alignment(green,capture,protocol)
            proof['alignment'].append(dict(source=capture['source'],scene=scene,role='noisy',**result))
            frame=dict(source=capture['source'],source_sha256=capture['source_sha256'],scene=scene,
                iso=capture['iso'],split=freeze['scene_partition'][scene],tiles=[],accepted=False)
            proof['frames'].append(frame)
            if not result['accepted']:
                frame['reason']='registration_failed_quality_gate';continue
            shift=result['shift_xy']
            refs=[warp(r,shift) for r in reference_images]
            masks=[warp(m,shift,mask=True)>0 for m in reference_masks]
            noisy=[read_plane(capture,d) for d in capture['tiles']]
            gains=balanced_gains(capture['camera_neutral'])
            masks=[m&np.all(n<protocol['unsaturated_camera_fraction'],axis=2) for m,n in zip(masks,noisy)]
            try:
                slope,offset=affine_fit(refs,noisy,masks,protocol['fitting_rois'])
            except ValueError as error:
                frame['reason']=str(error);continue
            frame['radiometry']=dict(gain=slope.tolist(),offset=offset.tolist(),
                fitting_rois=protocol['fitting_rois'],monitoring_rois=protocol['monitoring_rois'])
            if (np.any(slope<protocol['minimum_affine_gain']) or
                np.any(slope>protocol['maximum_affine_gain']) or
                np.any(np.abs(offset)>protocol['maximum_affine_offset']*gains)):
                frame['reason']='global_radiometry_outside_frozen_bounds';continue
            candidates=[]
            for roi,(n,r,m) in enumerate(zip(noisy,refs,masks)):
                target=(r*slope+offset).astype(np.float32)
                sigma=sigma_proxy(n)
                valid,summary=tile_quality(n,target,m,gains,sigma,protocol)
                frame['tiles'].append(dict(roi=roi,sigma_proxy=sigma.tolist(),**summary))
                candidates.append((n,target,valid,sigma,summary))
            monitors=sum(candidates[i][-1]['accepted'] for i in protocol['monitoring_rois'])
            frame['consistent_monitoring_rois']=monitors
            if monitors<2:
                frame['reason']='fewer_than_two_consistent_monitoring_rois';continue
            frame['accepted']=True;frame['reason']='exploratory_pair_qualified'
            for roi,(n,target,valid,sigma,summary) in enumerate(candidates):
                if not summary['accepted']:continue
                for patch in patches(n,target,valid,sigma,protocol):
                    identity=dict(id=f'{Path(capture["source"]).stem}-r{roi}-p{patch["origin_xy"]}',
                        source=capture['source'],source_sha256=capture['source_sha256'],scene_group=scene,
                        split=frame['split'],iso=capture['iso'],roi=roi,patch_xy=patch['origin_xy'],
                        content_sha256=hashlib.sha256(patch['signal'].tobytes()).hexdigest(),
                        target_sha256=hashlib.sha256(patch['target'].tobytes()).hexdigest())
                    proof['examples'].append(identity)
                    all_signals.append(patch['signal']);all_targets.append(patch['target'])
                    all_masks.append(patch['valid']);all_sigmas.append(patch['sigma'])
            print(f'PAIR {Path(capture["source"]).name} monitor={monitors}/4',flush=True)
    counts={role:sum(r['split']==role for r in proof['examples']) for role in ('train','validation','test')}
    proof['split_counts']=counts
    enough=(counts['train']>=protocol['minimum_training_patches'] and
            counts['validation']>=protocol['minimum_validation_patches'])
    proof['status']='qualified_for_exploratory_pilot' if enough else 'insufficient_qualified_pairs_no_training'
    if enough:
        destination=work/'dataset.npz'
        if destination.exists():raise ValueError('Dataset exists')
        np.savez(destination,signal=np.stack(all_signals),target=np.stack(all_targets),
            valid=np.stack(all_masks),sigma=np.stack(all_sigmas))
        proof['dataset']=dict(path=str(destination),sha256=digest(destination))
    with (work/'pairs.json').open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,indent=2,allow_nan=False)
    print('PAIR STATUS '+proof['status']+' '+str(counts),flush=True)
    return proof
