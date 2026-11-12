"""Bounded screening of low-ISO single RAW candidates, not clean-target qualification."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess

import numpy as np

from audit_moon import digest
from audit_capture_series import small_preview
from run_photo_pilot import checked_output,save


def select(records):
    candidates=[r for r in records if r['role']=='unqualified_single_or_sequence'
        and 0<r['metadata'].get('iso',99999)<=800
        and 0<r['metadata'].get('focal_mm',0)<=105
        and 0<r['metadata'].get('exposure_s',99)<=.004
        and len(str(r['metadata'].get('datetime_original','')))>=10]
    candidates.sort(key=lambda r:(r['metadata']['iso'],r['metadata']['datetime_original'],r['path']))
    selected=[];days=set()
    for record in candidates:
        day=record['metadata']['datetime_original'][:10]
        if day in days:continue
        selected.append(record);days.add(day)
        if len(selected)==6:break
    return candidates,selected


def run(source,output):
    root,output=checked_output(output)
    parent=json.loads(source.read_text(encoding='utf-8'))
    candidates,selected=select(parent['records'])
    now=datetime.now(timezone.utc)
    campaign='neural-low-noise-source-screen-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(source.resolve()),sha256=digest(source)),
        selection='At most six unqualified R6III single images, ISO<=800, focal<=105mm, exposure<=1/250s, one per capture day, ISO/date/path order.',
        interpretation='Prioritization only, not proof of brightness, sharpness, low RAW noise or scene diversity.',
        selected=selected,tools={n:digest(Path(__file__).with_name(n)) for n in
             ('screen_low_noise_sources.py','audit_capture_series.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    rows=[]
    for record in selected:
        path=Path(record['path']);before=path.stat()
        if (before.st_size,before.st_mtime_ns)!=(record['bytes'],record['mtime_ns']):
            raise ValueError('Source changed since inventory')
        rgb,size=small_preview(path);array=np.asarray(rgb).astype(np.float32)/255
        after=path.stat()
        if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
            raise ValueError('Source changed during preview read')
        rows.append(dict(path=str(path),source_sha256=digest(path),metadata=record['metadata'],
            original_jpeg_size_wh=size,preview_size_wh=list(rgb.size),
            encoded_rgb_q01_q50_q99=np.quantile(array.reshape(-1,3),[.01,.5,.99],axis=0).tolist(),
            encoded_any_channel_white_fraction=float(np.any(array>=254/255,axis=2).mean()),
            encoded_all_channels_dark_fraction=float(np.all(array<=2/255,axis=2).mean())))
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),eligible_file_records=len(candidates),
        screened=rows,status='candidates_only_not_clean_training_targets',
        limits=['Encoded JPEG statistics are not RAW clipping or calibrated radiometry.',
            'Different capture days do not prove different physical scenes or independent content.',
            'Single-image sources have no independent clean ground truth or measured noise realization.',
            'No RAW development, new training, scene assignment, source copy/move or publication.'])
    save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.source,a.output)
