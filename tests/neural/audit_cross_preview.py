"""JPEG cross-ISO diagnostic only; never a RAW pair-admission criterion."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import platform

import cv2
import numpy as np

from audit_moon import digest
from audit_capture_series import small_preview
from photo_pair_math import translation


def run(captures_path):
    cv2.setNumThreads(2)
    root=Path(__file__).resolve().parents[2]
    records=json.loads(captures_path.read_text(encoding='utf-8'))['records']
    selected=('6V1A0134','6V1A0152','6V1A0187','6V1A0195',
              '6V1A0211','6V1A0244','6V1A0263','6V1A0281')
    records=[r for r in records if Path(r['source']).stem in selected]
    if len(records)!=8:raise ValueError('Incomplete fixed preview selection')
    rows=[]
    for scene in ('indoor','vegetation'):
        group=[r for r in records if r['scene']==scene]
        reference=None
        for record in group:
            path=Path(record['source']);before=path.stat()
            if digest(path)!=record['source_sha256']:raise ValueError('Source changed')
            preview,size=small_preview(path)
            gray=cv2.cvtColor(np.asarray(preview),cv2.COLOR_RGB2GRAY).astype(np.float32)/255.
            if reference is None:reference=gray
            result=translation(reference,gray,[1.,1.],camera_limit=2.)
            after=path.stat()
            if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
                raise ValueError('Source changed during JPEG read')
            rows.append(dict(source=str(path),source_sha256=record['source_sha256'],scene=scene,
                iso=record['iso'],preview_size_wh=list(preview.size),embedded_size_wh=size,registration=result))
    stamp=datetime.now(timezone.utc)
    proof=dict(schema_version=1,data_version=1,
        campaign_id='neural-preview-cross-check-v001-'+stamp.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=stamp.isoformat(),captures_sha256=digest(captures_path),
        source_hashes={p.name:digest(p) for p in (Path(__file__),Path(__file__).with_name('photo_pair_math.py'),
            Path(__file__).with_name('audit_capture_series.py'))},
        versions=dict(python=platform.python_version(),numpy=np.__version__,opencv=cv2.__version__),
        protocol='Fixed 8 previews; max800 pixels; encoded cv2 RGB-to-gray /255, not linear RAW; ECC translation, common crop8, Gaussian1.5+5; no JPEG saturation exclusion.',
        units=dict(ecc='dimensionless',translation='preview pixels'),records=rows,
        limits=['Diagnostic of cross-ISO visual correspondence only, never RAW truth or noise calibration.',
            'Does not establish motion vs lighting vs focus; no training or pair admission based on JPEG scores.'])
    output=root/'docs/validation/raw'/(proof['campaign_id']+'.json')
    with output.open('x',encoding='utf-8') as handle:json.dump(proof,handle,indent=2,allow_nan=False)
    print(output)
    for row in rows:print(Path(row['source']).stem,row['registration'].get('ecc'))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--captures',type=Path,required=True)
    run(p.parse_args().captures)
