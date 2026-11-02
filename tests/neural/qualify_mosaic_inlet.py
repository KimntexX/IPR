"""Qualify real signed-CFA capture against frozen production RGB, no training."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

import numpy as np

from audit_moon import digest
from mosaic_data import pack_mosaic,unpack_mosaic,pattern_gains,read_stage
from run_photo_pilot import checked_output,save


def selected_captures(old,burst):
    records=old['extraction']['records']+burst['captures']
    selected=[]
    for name,iso,scene in [('6V1A0134.CR3',100,'indoor'),('6V1A0181.CR3',12800,'indoor'),
                           ('6V1A9819.CR3',4000,'burst_4000_training_candidate')]:
        rows=[r for r in records if Path(r['source']).name==name and r['iso']==iso and r['scene']==scene]
        if len(rows)!=1:raise ValueError('Missing or ambiguous frozen source '+name)
        selected.append(rows[0])
    return selected


def measure(capture):
    if not capture['rgb_replay_bit_exact'] or capture['rgb_regions_compared']!=9:
        raise ValueError('Production RGB replay failed')
    if len(capture['stages'])!=18:raise ValueError('Missing mosaic stages')
    rows=[]
    for roi in range(9):
        stages={s['stage']:s for s in capture['stages'] if s['roi']==roi}
        before=stages['normalized_cfa_before_wb_unclipped'];after=stages['balanced_cfa_pre_rcd_white_clipped']
        if before['pattern']!=after['pattern'] or before['origin_xy']!=after['origin_xy']:
            raise ValueError('CFA phase/origin changed between stages')
        planes=[];stats=[]
        for stage in (before,after):
            image=read_stage(capture,stage);packed=pack_mosaic(image)
            if not np.array_equal(unpack_mosaic(packed),image):raise ValueError('Packing changed sensor values')
            planes.append(packed)
            stats.append(dict(stage=stage['stage'],negative_fraction_per_position=(packed<0).mean(axis=(1,2)).tolist(),
                at_or_above_white_fraction_per_position=(packed>=1).mean(axis=(1,2)).tolist(),
                q01_q50_q99_per_position=np.quantile(packed,[.01,.5,.99],axis=(1,2)).tolist()))
        gains=pattern_gains(before['pattern'],capture['gains'])[:,None,None]
        expected=np.minimum(planes[0]*gains,1.)
        maximum=float(np.max(np.abs(expected-planes[1])))
        if not np.allclose(expected,planes[1],rtol=1e-6,atol=1e-7):raise ValueError('WB/white-bound relation failed')
        rows.append(dict(roi=roi,origin_xy=before['origin_xy'],pattern=before['pattern'],packed_shape=[4,192,192],
            roundtrip_exact=True,wb_relation_max_abs_error=maximum,stages=stats))
    return rows


def run(args):
    root,work=checked_output(args.output)
    old=json.loads(args.parent.read_text(encoding='utf-8'));burst=json.loads(args.burst.read_text(encoding='utf-8'))
    captures=selected_captures(old,burst)
    for c in captures:
        subprocess.run(['git','diff','--exit-code',c['engine_revision'],'HEAD','--','src','config/raw-normalization.json'],
            cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
        if digest(Path(c['descriptor']))!=c['descriptor_sha256']:raise ValueError('Changed frozen camera proof')
    now=datetime.now(timezone.utc);campaign='neural-mosaic-inlet-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={n:dict(path=str(getattr(args,n).resolve()),sha256=digest(getattr(args,n))) for n in ('parent','burst')},
        tool=dict(path=str(args.tool.resolve()),sha256=digest(args.tool)),sources=captures,
        policy='Three frozen training-side Bayer sources; no control-dependent selection or neural fitting.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('qualify_mosaic_inlet.py','mosaic_data.py','capture_mosaic.cpp')})
    work.mkdir(parents=True,exist_ok=False);save(work/'freeze.json',freeze)
    env=dict(os.environ,OMP_NUM_THREADS='2',OPENBLAS_NUM_THREADS='2')
    for location in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(location).is_dir():env['PATH']=location+os.pathsep+env.get('PATH','')
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(work/'freeze.json'),records=[],status='incomplete',
        limits=['Input-contract qualification on three R6III RAWs, not joint-model quality or other-camera qualification.',
            'Balanced pre-RCD stage already clips at white; unbalanced normalized stage preserves pre-gain over-white values.',
            'Packing uses spatial TL/TR/BL/BR positions with explicit CFA colour tags, not assumed RGGB order.',
            'Existing RGB burst targets inherit RCD/median; not independent demosaicking ground truth.',
            'No new photos, source modification, training, default behavior change or production integration.'])
    started=time.monotonic()
    try:
        with (work/'self-test.log').open('x',encoding='utf-8') as log:
            subprocess.run([str(args.tool.resolve()),'--self-test'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
        for c in captures:
            destination=work/(Path(c['source']).stem+'-mosaic.json')
            with destination.with_suffix('.log').open('x',encoding='utf-8') as log:
                subprocess.run([str(args.tool.resolve()),'--camera-proof',c['descriptor'],'--output',str(destination)],
                    env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
            capture=json.loads(destination.read_text(encoding='utf-8'))
            capture.update(descriptor=str(destination),descriptor_sha256=digest(destination))
            if (capture['source_sha256']!=c['source_sha256'] or capture['camera_proof_sha256']!=c['descriptor_sha256']
                    or digest(destination.parent/capture['pixels'])!=capture['pixels_sha256']):raise ValueError('Capture identity mismatch')
            result['records'].append(dict(capture=capture,measurements=measure(capture)))
            print('QUALIFIED '+Path(c['source']).name,flush=True)
        if digest(args.tool)!=freeze['tool']['sha256']:raise ValueError('Tool changed during run')
        result['status']='qualified_signed_mosaic_inlet_not_joint_model'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(work/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('parent','burst','tool','output'):p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())
