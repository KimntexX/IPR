"""N2b(a) candidate on the frozen LR/A/B masks; no duplicate baseline campaign."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.analysis.render import RenderServer
from tests.analysis.native import sha256


def luma(im):
    return .2126*im[...,0]+.7152*im[...,1]+.0722*im[...,2]


def masks(ref):
    # Same float32 recipe as the embedded strength_law.py, verified by packbits hashes.
    y=luma(ref); smooth=cv2.GaussianBlur(y,(0,0),1.5)
    gradient=np.hypot(cv2.Sobel(smooth,cv2.CV_32F,1,0),cv2.Sobel(smooth,cv2.CV_32F,0,1))
    low,high=np.percentile(gradient,50),np.percentile(gradient,90)
    return dict(dark=(y<25)&(gradient<high),mid_flat=(y>=25)&(y<120)&(gradient<low),
                bright_flat=(y>=120)&(gradient<low),edges=gradient>=high)


def metrics(im, masks):
    y=luma(im); hp=lambda x:x-cv2.GaussianBlur(x,(0,0),2)
    hy=hp(y); hc=[hp(im[...,0]-im[...,1]),hp(im[...,2]-im[...,1])]
    return {name: dict(luma=round(float(y[mask].mean()),3),luma_noise=round(float(hy[mask].std()),3),
                       chroma_noise=round(float(np.mean([c[mask].std() for c in hc])),3))
            for name,mask in masks.items() if mask.sum()>=2000}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server',type=Path,required=True)
    p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--image-root',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    a=p.parse_args();stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    os.environ['PATH']=os.pathsep.join(str(d.resolve()) for d in a.runtime_dir)+os.pathsep+os.environ.get('PATH','')
    reference=json.loads(a.reference.read_text(encoding='utf-8'))
    if reference['schema']!='ipr-noise-strength-law-v001':raise ValueError('Unknown reference schema')
    development=['7Y6A7599','6V1A9539','IMG_7958']
    proof=dict(schema_version=1,created_utc=stamp,study='N2b_signal_covariance_candidate',
               reference=dict(path=str(a.reference),sha256=sha256(a.reference)),
               engine_base_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
               executable_sha256=sha256(a.server),manifest_sha256=sha256(ROOT/'config/raw-normalization.json'),
               sources={n:dict(sha256=sha256(ROOT/n),text=(ROOT/n).read_text(encoding='utf-8')) for n in
                        ['src/raw/CameraDenoise.cpp','src/raw/CameraDenoise.h','tests/raw_bench/qualify_signal_denoise.py']},
               development_scenes=development,reserved_for_frozen_followup=sorted(set(reference['scenes'])-set(development)),
               limits=['Reserve is prospective only; this corpus already had earlier studies.',
                       'Highpass includes real structure; lower is not necessarily better.',
                       'Noise estimate and covariance changed, not slider law, DCP or output bounds.',
                       'This is not a clean-noise oracle or visual validation.'],scenes=[])
    failed=False
    for name in development:
        old=reference['scenes'][name]
        raw=a.image_root/Path(old['raw']['file']).name
        neutral=a.image_root/'neutre'/Path(old['lr_neutral']['file']).name
        area=old['masks']['common_area'];lx,ly=area['lr_origin_xy'];ix,iy=area['ipr_origin_xy'];w,h=area['size_wh']
        item=dict(scene=name,states={},alignment=old['alignment'],common_area=area)
        proof['scenes'].append(item)
        server=None
        try:
            if sha256(raw)!=old['raw']['sha256'] or sha256(neutral)!=old['lr_neutral']['sha256']:
                raise ValueError('Source/reference identity changed')
            image=cv2.imread(str(neutral),cv2.IMREAD_UNCHANGED)
            if image is None:raise ValueError('Cannot read LR neutral')
            rgb=cv2.cvtColor(image[ly:ly+h,lx:lx+w,:3],cv2.COLOR_BGR2RGB).astype(np.float32)/257.
            del image
            fixed=masks(rgb);del rgb
            hashes={n:hashlib.sha256(np.packbits(m).tobytes()).hexdigest() for n,m in fixed.items()}
            item['mask_hashes']=hashes
            if hashes!=old['masks']['sha256_packbits']:raise ValueError('Frozen masks differ')
            server=RenderServer(a.server.resolve(),timeout=10)
            for lum,col in [(0,0),(50,0),(100,0),(0,100),(100,100)]:
                guard=threading.Timer(240,server.process.kill);guard.start()
                try:
                    start=time.monotonic()
                    pixels,metadata=server.render(raw,'neutral',0,camera_denoise=dict(luminance=lum,chrominance=col))
                    elapsed=time.monotonic()-start
                    if [pixels.shape[1],pixels.shape[0]]!=old['alignment']['ipr_size_wh']:raise ValueError('IPR geometry changed')
                    selected=pixels[iy:iy+h,ix:ix+w].astype(np.float32)/257.;del pixels
                    result=metrics(selected,fixed);del selected
                    key=f'{lum}/{col}/0'
                    item['states'][key]=dict(metrics=result,metadata=metadata,request_wall_seconds=elapsed,
                        baseline_B=old['measures']['B'][key])
                    if lum==0 and col==0:
                        item['B0_metrics_match']=result==old['measures']['B'][key]
                        if not item['B0_metrics_match']:raise ValueError('B0 control metrics differ')
                    print(name,key,elapsed,flush=True)
                finally:
                    guard.cancel();guard.join()
        except Exception as exc:
            failed=True;item['error']=repr(exc)
        finally:
            if server is not None:server.close()
        if failed:break
    proof['verdict']='fail' if failed else 'measured_candidate_not_visual_validation'
    output=ROOT/'docs/validation/raw'/f'camera-denoise-n2b-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:json.dump(proof,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,proof['verdict'],flush=True)
    return int(failed)


if __name__=='__main__':sys.exit(main())
