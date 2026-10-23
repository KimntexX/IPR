"""Two-scene diagnostic: native TIFF comparisons and in-memory IPR opening states.

No source mutation, IPR TIFF generation, clean-ground-truth score or production change.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import platform
import subprocess
import threading

import cv2
import numpy as np
from PIL import Image, ImageDraw

from analysis.native import inspect, signature, sha256, write_immutable
from analysis.render import RenderServer
from analyse_color_mixer import linearize, luminance
from analyse_noise import region_candidates, select_regions, read_patch, plane_metrics
from inspect_saved_raw_state import inspect as inspect_sidecar


def metrics(encoded):
    result=plane_metrics(encoded)
    rgb=linearize(encoded)
    y=luminance(rgb)
    co=rgb[:,:,0]-rgb[:,:,2]
    cg=rgb[:,:,1]-.5*(rgb[:,:,0]+rgb[:,:,2])
    def band(plane):
        return cv2.GaussianBlur(plane,(0,0),1.)-cv2.GaussianBlur(plane,(0,0),4.)
    def mad(plane):
        return float(np.median(np.abs(plane-np.median(plane)))/.67448975)
    result['medium_band_Y_mad']=mad(band(y))
    result['medium_band_chroma_mad']=float(np.hypot(mad(band(co)),mad(band(cg)))/np.sqrt(2))
    result['relative_sigma_Y']=result['sigma_Y']/max(result['mean_Y'],1e-6)
    return result


def matched_roi(reference, target, roi, margin=32):
    """Integer translation on blurred luminance only; no pixel interpolation for metrics."""
    size=roi['size']
    top=round(roi['top']*(target.shape[0]-size)/max(reference.shape[0]-size,1))
    left=round(roi['left']*(target.shape[1]-size)/max(reference.shape[1]-size,1))
    y0,x0=max(0,top-margin),max(0,left-margin)
    y1,x1=min(target.shape[0],top+size+margin),min(target.shape[1],left+size+margin)
    template=luminance(linearize(read_patch(reference,roi)))
    window=luminance(linearize(np.asarray(target[y0:y1,x0:x1,:3],dtype=np.float32)/65535.))
    template=cv2.GaussianBlur(template,(0,0),2.)
    window=cv2.GaussianBlur(window,(0,0),2.)
    if template.std()<1e-7:
        return dict(top=top,left=left,size=size),dict(status='unregistered_flat',ncc=None)
    scores=cv2.matchTemplate(window,template,cv2.TM_CCOEFF_NORMED)
    _,score,_,position=cv2.minMaxLoc(scores)
    return dict(top=y0+position[1],left=x0+position[0],size=size),dict(
        status='accepted_integer_translation' if score>=.8 else 'unreliable', ncc=float(score),
        offset_yx=[y0+position[1]-top,x0+position[0]-left], search_margin_pixels=margin)


def compare(reference,target,rois):
    records=[]
    keys=('sigma_Y','relative_sigma_Y','sigma_chroma','medium_band_Y_mad','medium_band_chroma_mad',
          'mean_Y','gradient_mean')
    for roi in rois:
        target_roi,registration=matched_roi(reference,target,roi)
        a,b=metrics(read_patch(reference,roi)),metrics(read_patch(target,target_roi))
        records.append(dict(reference_roi=roi,target_roi=target_roi,registration=registration,
            reference=a,target=b,ratios={k:b[k]/a[k] if a[k]>1e-12 else None for k in keys}))
    summary={}
    for kind in ('flat','detail'):
        selected=[r for r in records if r['reference_roi']['kind']==kind and
                  r['registration']['status']=='accepted_integer_translation']
        summary[kind]=dict(accepted_regions=len(selected),
            attempted_regions=sum(r['reference_roi']['kind']==kind for r in records),
            median_target_to_reference={k:float(np.median([r['ratios'][k] for r in selected
                if r['ratios'][k] is not None])) if any(r['ratios'][k] is not None for r in selected) else None
                                       for k in keys})
    return dict(regions=records,summary=summary)


def render_neutral(server,source):
    timer=threading.Timer(180,server.process.kill)
    timer.daemon=True
    timer.start()
    try:
        return server.render(source,'neutral',0)
    finally:
        timer.cancel()
        timer.join(timeout=5)


def save_crops(images,rois,path):
    names=list(images)
    canvas=Image.new('RGB',(len(names)*256, len(rois)*282),(32,32,32))
    draw=ImageDraw.Draw(canvas)
    reference=images[names[0]]
    for row,roi in enumerate(rois):
        for col,name in enumerate(names):
            target=images[name]
            box=roi if name==names[0] else matched_roi(reference,target,roi)[0]
            crop=Image.fromarray(np.round(np.clip(read_patch(target,box),0,1)*255).astype(np.uint8))
            canvas.paste(crop,(col*256,row*282+26))
            draw.text((col*256+4,row*282+5),name+' '+roi['kind'],fill='white')
    canvas.save(path)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base',type=Path,required=True)
    parser.add_argument('--server',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--figures',type=Path,required=True)
    args=parser.parse_args()
    now=datetime.now(timezone.utc)
    args.figures.mkdir(parents=True,exist_ok=True)
    result=dict(campaign_id='noise-prototype-comparison-v001-'+now.strftime('%Y%m%dT%H%M%SZ'),
        schema_version=1,data_version=1,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
        source_sha256={p:sha256(Path(p)) for p in ('tests/study_noise_exports.py','tests/analyse_noise.py',
            'src/core/NoiseEstimation.cpp','src/core/DetailProcessing.cpp','src/editor/imageeditor.cpp')},
        versions=dict(python=platform.python_version(),numpy=np.__version__,opencv=cv2.__version__),
        configuration=dict(patch_size=256,selection='six low-structure and four detail candidates on LR neutral',
            output_domain='encoded and linearized sRGB uint16 samples; not sensor sigma',
            registration='integer local search on sigma-2 blurred Y; threshold NCC 0.8; no warp or resampling',
            metrics='Haar HH MAD; medium-band MAD (Gaussian sigma1 minus sigma4); gradient and mean',
            limitations=['No clean ground truth or AI-reference image supplied',
                         'Texture, demosaic correlations and local tone differences affect noise proxies',
                         'Matched-region summary excludes NCC below 0.8; flat uniform regions may fail registration',
                         'Opening: optics off on both engines; received prototype exports: optics on user-declared',
                         'Do not infer pure denoising gain from optics-off neutral to optics-on processed state',
                         'No cross-engine MAE/PSNR/SSIM as fidelity to a clean image',
                         'Current sidecar is not proof of export-time settings']),
        audit=dict(production_modified=False,source_images_modified=False,ipr_tiffs_generated=False),scenes=[])
    with RenderServer(args.server,timeout=10) as server:
        result['render_server_sha256']=server.sha256
        for scene in ('6V1A9608','6V1A9890'):
            images,meta,snapshots={},{},{}
            for name,path in (('LR_neutral',args.base/'neutre'/f'{scene}.tif'),
                              ('LR_100',args.base/'bruit/proto'/f'{scene}.tif'),
                              ('IPR_prototype',args.base/'bruit/proto'/f'{scene}-IPR.tiff')):
                images[name],meta[name],snapshots[name]=inspect(path)
            for name,amount in (('LR_neutral',0),('LR_100',100)):
                s=meta[name]['xmp_settings']
                if any(float(s.get(k,'nan'))!=v for k,v in
                       (('Sharpness',0),('LuminanceSmoothing',amount),('ColorNoiseReduction',amount))):
                    raise ValueError('Invalid LR noise/sharpening state; stop before measurements: '+meta[name]['path'])
                if s.get('CameraProfile')!='Camera Neutral': raise ValueError('Unexpected LR profile')
            source=args.base/f'{scene}.CR3'
            raw_before=signature(source)
            raw_sha=sha256(source)
            images['IPR_neutral'],meta['IPR_neutral']=render_neutral(server,source)
            candidates=region_candidates(images['LR_neutral'])
            flats=select_regions(candidates,'flat',6)
            occupied={(r['top'],r['left']) for r in flats}
            rois=flats+select_regions([r for r in candidates if (r['top'],r['left']) not in occupied],'detail',4)
            if not flats or not any(r['kind']=='detail' for r in rois):
                raise ValueError('Insufficient eligible flat/detail regions for '+scene)
            sidecar=args.base/f'{scene}.CR3.ipr'
            record=dict(scene=scene,inputs=meta,raw_source=dict(path=str(source),sha256=raw_sha),regions=rois,
                current_sidecar=inspect_sidecar(sidecar) if sidecar.is_file() else None,
                opening=compare(images['LR_neutral'],images['IPR_neutral'],rois),
                processed_exports=compare(images['LR_100'],images['IPR_prototype'],rois),
                protocol_valid_for_pure_denoising_gain=False)
            save_crops({k:images[k] for k in ('LR_neutral','IPR_neutral','LR_100','IPR_prototype')},
                       [flats[0],next(r for r in rois if r['kind']=='detail')],args.figures/f'{scene}-crops.png')
            for name,before in snapshots.items():
                if signature(meta[name]['path'])!=before or sha256(meta[name]['path'])!=meta[name]['sha256']:
                    raise ValueError('Source changed during comparison')
            if signature(source)!=raw_before: raise ValueError('RAW changed during comparison')
            result['scenes'].append(record)
            print(scene, 'opening', record['opening']['summary']['flat'],flush=True)
            print(scene, 'processed',record['processed_exports']['summary']['flat'],flush=True)
            del images
    write_immutable(args.output,result)
    print(args.output)


if __name__=='__main__':
    main()
