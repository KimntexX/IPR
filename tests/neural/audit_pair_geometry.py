"""Diagnostic geometry audit on immutable camera captures; no pair promotion.

Compare translation, rigid and affine fits with fixed radiometric control ROIs.
Reference crops alone are interpolated. The original 12px gate is not changed.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

import cv2
import numpy as np

from audit_moon import digest
from photo_pair_math import (blur,balanced_gains,reference_tile,affine_fit,
                             sigma_proxy,tile_quality)
from qualify_photo_pairs import read_plane
from run_photo_pilot import checked_output,save


MODELS=dict(translation=cv2.MOTION_TRANSLATION,rigid=cv2.MOTION_EUCLIDEAN,
            affine=cv2.MOTION_AFFINE)


def fit_geometry(reference,source,full_shape,model,camera_limit=.9):
    if (model not in MODELS or reference.shape!=source.shape or reference.ndim!=2
            or min(reference.shape)<32 or not np.isfinite(reference).all()
            or not np.isfinite(source).all()):
        raise ValueError('Matching finite green proxies and a known model required')
    arrays=[]
    for image in (reference,source):
        value=blur(image.astype(np.float32),1.5)[8:-8,8:-8]
        std=float(value.std())
        if std<1e-6:
            return dict(status='unobservable_flat_registration')
        arrays.append((value-float(value.mean()))/std)
    mask=cv2.erode(((reference<camera_limit)&(source<camera_limit)).astype(np.uint8),
                   np.ones((9,9),np.uint8))[8:-8,8:-8]
    if np.count_nonzero(mask)<256:
        return dict(status='insufficient_unsaturated_support')
    try:
        ecc,matrix=cv2.findTransformECC(*arrays,np.eye(2,3,dtype=np.float32),MODELS[model],
            (cv2.TERM_CRITERIA_COUNT|cv2.TERM_CRITERIA_EPS,80,1e-6),mask,5)
    except cv2.error as error:
        return dict(status='registration_failed',error=str(error))
    # Undo the identical 8px proxy crop before mapping back to full camera axes.
    linear=matrix[:,:2].astype(np.float64)
    offset=matrix[:,2].astype(np.float64)+np.array([8.,8.])-linear@np.array([8.,8.])
    scale=np.array([full_shape[1]/source.shape[1],full_shape[0]/source.shape[0]])
    full=np.column_stack((np.diag(scale)@linear@np.diag(1/scale),offset*scale))
    corners=np.array([[0,0],[full_shape[1]-1,0],[0,full_shape[0]-1],
                      [full_shape[1]-1,full_shape[0]-1]],np.float64)
    delta=corners@full[:,:2].T+full[:,2]-corners
    return dict(status='measured',ecc=float(ecc),reference_to_source_full=full.tolist(),
        center_shift_xy=(full[:,:2]@np.array([full_shape[1]/2,full_shape[0]/2])+
                         full[:,2]-np.array([full_shape[1]/2,full_shape[0]/2])).tolist(),
        maximum_corner_shift_pixels=float(np.linalg.norm(delta,axis=1).max()),
        rotation_degrees=float(np.degrees(np.arctan2(full[1,0],full[0,0]))),
        determinant=float(np.linalg.det(full[:,:2])))


def local_transform(full,origin_xy):
    matrix=np.asarray(full,dtype=np.float64)
    origin=np.asarray(origin_xy,dtype=np.float64)
    if matrix.shape!=(2,3) or origin.shape!=(2,) or not np.isfinite(matrix).all() or not np.isfinite(origin).all():
        raise ValueError('Finite affine transform and tile origin required')
    local=matrix.copy()
    local[:,2]=matrix[:,:2]@origin+matrix[:,2]-origin
    return local.astype(np.float32)


def transformed_reference(reference,valid,matrix):
    if valid.shape!=reference.shape[:2] or valid.dtype!=bool:
        raise ValueError('Matching reference mask required')
    size=(reference.shape[1],reference.shape[0])
    image=cv2.warpAffine(reference,matrix,size,flags=cv2.INTER_LINEAR,
                         borderMode=cv2.BORDER_CONSTANT,borderValue=0)
    # Require all interpolation neighbors to be valid, not only the nearest one.
    support=cv2.erode(valid.astype(np.uint8),np.ones((3,3),np.uint8))
    support=cv2.warpAffine(support,matrix,size,flags=cv2.INTER_NEAREST,
                           borderMode=cv2.BORDER_CONSTANT,borderValue=0)>0
    return image,support


def audit_frame(reference,capture,images,masks,protocol,model):
    green=read_plane(reference,reference['green_proxy'])[:,:,0]
    noisy_green=read_plane(capture,capture['green_proxy'])[:,:,0]
    geometry=fit_geometry(green,noisy_green,capture['full_shape'],model,
                          protocol['unsaturated_camera_fraction'])
    result=dict(source=capture['source'],source_sha256=capture['source_sha256'],
                iso=capture['iso'],model=model,geometry=geometry,tiles=[])
    if geometry['status']!='measured': return result
    targets=[];supports=[];noisy=[]
    for roi,(image,mask) in enumerate(zip(images,masks)):
        if reference['tiles'][roi]['origin_xy']!=capture['tiles'][roi]['origin_xy']:
            raise ValueError('Unexpected crop-origin change')
        local=local_transform(geometry['reference_to_source_full'],reference['tiles'][roi]['origin_xy'])
        target,support=transformed_reference(image,mask,local)
        signal=read_plane(capture,capture['tiles'][roi])
        targets.append(target);noisy.append(signal)
        supports.append(support&np.all(signal<protocol['unsaturated_camera_fraction'],axis=2))
    try:
        gain,offset=affine_fit(targets,noisy,supports,protocol['fitting_rois'])
    except ValueError as error:
        result['radiometry_error']=str(error);return result
    wb=balanced_gains(capture['camera_neutral'])
    result['radiometry']=dict(gain=gain.tolist(),offset=offset.tolist(),
        within_original_bounds=bool(np.all(gain>=protocol['minimum_affine_gain']) and
            np.all(gain<=protocol['maximum_affine_gain']) and
            np.all(np.abs(offset)<=protocol['maximum_affine_offset']*wb)))
    for roi,(signal,target,mask) in enumerate(zip(noisy,targets,supports)):
        _,quality=tile_quality(signal,(target*gain+offset).astype(np.float32),mask,wb,
                               sigma_proxy(signal),protocol)
        result['tiles'].append(dict(roi=roi,**quality))
    result['consistent_monitoring_rois']=sum(result['tiles'][i]['accepted'] for i in protocol['monitoring_rois'])
    result['ecc_passes_original_floor']=bool(geometry['ecc']>=protocol['ecc_floor'])
    return result


def run(source,output):
    root,output=checked_output(output)
    parent=json.loads(source.read_text(encoding='utf-8'))
    if parent.get('error') or len(parent['captures'])!=29:
        raise ValueError('Expected the completed 29-capture control study')
    for c in parent['captures']:
        if digest(c['descriptor'])!=c['descriptor_sha256'] or digest(Path(c['descriptor']).parent/c['pixels'])!=c['pixels_sha256']:
            raise ValueError('Changed immutable capture')
    output.mkdir(parents=True,exist_ok=False)
    now=datetime.now(timezone.utc);started=time.monotonic()
    campaign='neural-geometry-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,
        measured_at_utc=now.isoformat(),source=dict(path=str(source.resolve()),sha256=digest(source)),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        tools={name:digest(Path(__file__).with_name(name)) for name in
               ('audit_pair_geometry.py','photo_pair_math.py','qualify_photo_pairs.py')},
        models=list(MODELS),reference_groups=[1,3],qualification=parent['freeze']['qualification'],
        interpretation='Diagnostics only; no larger-shift pair promotion or choice of reference/model after outcomes.')
    save(output/'freeze.json',freeze)
    cv2.setNumThreads(2)
    records=[];reference_proofs=[]
    for variant in parent['variants']:
        group=variant['freeze']['reference_group'];q=freeze['qualification']
        refs=[c for c in parent['captures'] if c['iso']==100 and c['group']==group]
        alignment={a['source']:a for a in variant['pairs']['alignment'] if a['role']=='reference'}
        if not all(alignment[c['source']]['accepted'] for c in refs):
            raise ValueError('Reference exclusions require a separate protocol')
        images=[];masks=[]
        for roi in range(9):
            mean,mask,summary=reference_tile([read_plane(c,c['tiles'][roi]) for c in refs],
                [alignment[c['source']]['shift_xy'] for c in refs],
                [balanced_gains(c['camera_neutral']) for c in refs],q)
            images.append(mean);masks.append(mask)
            reference_proofs.append(dict(group=group,roi=roi,**summary))
        for capture in [c for c in parent['captures'] if c['iso']!=100]:
            for model in MODELS:
                result=audit_frame(refs[0],capture,images,masks,q,model)
                result['reference_group']=group;records.append(result)
            print(f'GEOMETRY ref={group} {Path(capture["source"]).name}',flush=True)
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,freeze=freeze,
        measured_at_utc=datetime.now(timezone.utc).isoformat(),freeze_sha256=digest(output/'freeze.json'),
        elapsed_seconds=time.monotonic()-started,versions=dict(numpy=np.__version__,opencv=cv2.__version__),
        captures=parent['captures'],references=reference_proofs,records=records,
        status='diagnostic_only_no_training',
        limits=['Original displacement gate and primary reference unchanged.',
                'No source resampling, no newly qualified pairs or neural model.',
                'Interpolation reduces common reference support; fixed tile masks are recomputed geometrically.',
                'Global geometry may not describe parallax, focus or object motion.',
                'Reference/geometry model comparison is not independent product validation.'])
    save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.source,a.output)
