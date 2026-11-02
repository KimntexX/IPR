"""Measure signed-camera ISO100 content supports; never claim clean targets."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

import cv2
import numpy as np

from audit_moon import digest
from photo_pair_math import finite_rgb,sigma_proxy
from qualify_control_scene import extract
from qualify_photo_pairs import read_plane
from run_photo_pilot import checked_output,save


def patch_diagnostics(image):
    finite_rgb(image)
    if image.shape!=(96,96,3):raise ValueError('Expected fixed 96px RGB patch')
    valid=np.all(image<.9,axis=2)
    low=cv2.GaussianBlur(image,(0,0),2.)
    residual=image-low
    gradient=np.diff(low,axis=1)
    return dict(valid_fraction=float(valid.mean()),
        usable_content_support=bool(valid.mean()>=.9),
        negative_fraction_rgb=(image<0).mean(axis=(0,1)).tolist(),
        signal_q10_q50_q90_rgb=np.quantile(image,[.1,.5,.9],axis=(0,1)).tolist(),
        haar_mad_proxy_rgb=sigma_proxy(image).tolist(),
        highpass_rms_rgb=np.sqrt(np.mean(residual**2,axis=(0,1))).tolist(),
        lowpass_horizontal_gradient_rms_rgb=np.sqrt(np.mean(gradient**2,axis=(0,1))).tolist())


def selected_entries(parent):
    rows=parent['screened']
    if len(rows)!=5 or len({r['source_sha256'] for r in rows})!=5:
        raise ValueError('Expected five distinct previously frozen sources')
    if any(r['metadata']['iso']!=100 or r['metadata']['model']!='Canon EOS R6 Mark III' for r in rows):
        raise ValueError('Unexpected source camera or ISO')
    return [dict(group=i,iso=100,scene='unassigned_low_iso_support',
        source=dict(destination=r['path'],destination_sha256=r['source_sha256'])) for i,r in enumerate(rows)]


def run(args):
    root,work=checked_output(args.output)
    parent=json.loads(args.source.read_text(encoding='utf-8'))
    entries=selected_entries(parent)
    if shutil.disk_usage(root).free<2*2**30:raise ValueError('Need 2 GiB reserve')
    now=datetime.now(timezone.utc)
    campaign='neural-low-iso-supports-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    identities={n:digest(getattr(args,n)) for n in ('tool','profile','manifest')}
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(args.source.resolve()),sha256=digest(args.source)),
        sources=entries,identities=identities,
        dependency_paths={n:str(getattr(args,n).resolve()) for n in identities},
        protocol=dict(patch_size=96,patch_origins=[24,144,264],unsaturated_camera_fraction=.9,
            minimum_valid_fraction=.9,highpass_sigma=2.,selection_by_noise_estimate=False),
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('qualify_low_iso_supports.py','qualify_control_scene.py','qualify_photo_pairs.py','photo_pair_math.py')})
    work.mkdir(parents=True,exist_ok=False);save(work/'freeze.json',freeze)
    cv2.setNumThreads(2)
    env=dict(os.environ,OMP_NUM_THREADS='2',OPENBLAS_NUM_THREADS='2')
    for location in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(location).is_dir():env['PATH']=location+os.pathsep+env.get('PATH','')
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(work/'freeze.json'),captures=[],regions=[],status='incomplete',
        training=None,limits=['Single images provide no measured clean target or independent noise realization.',
            'Haar MAD and highpass RMS mix sensor noise, demosaic correlation and texture; not physical sigma.',
            'No low-proxy selection, denoising, synthetic noise injection, training or scene partition assignment.',
            'Finite unsaturated support is not proof of sharpness, distinct content or clean-target suitability.',
            'Camera-domain clipping threshold is conservative and not proof of sensor clipping.'])
    started=time.monotonic()
    try:
        for entry in entries:
            capture=extract(entry,work,args.tool,args.profile,args.manifest,env,identities)
            result['captures'].append(capture)
            for roi,descriptor in enumerate(capture['tiles']):
                image=read_plane(capture,descriptor)
                for y in (24,144,264):
                    for x in (24,144,264):
                        result['regions'].append(dict(source=capture['source'],source_sha256=capture['source_sha256'],
                            roi=roi,patch_xy=[x,y],**patch_diagnostics(image[y:y+96,x:x+96])))
            print(f'CAPTURE {len(result["captures"])}/5 {Path(capture["source"]).name}',flush=True)
        if any(digest(getattr(args,n))!=v for n,v in identities.items()):raise ValueError('Dependency changed')
        result['status']='content_diagnostics_only_not_clean_target_qualification'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(work/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','tool','profile','manifest','output'):p.add_argument('--'+name,type=Path,required=True)
    run(p.parse_args())
