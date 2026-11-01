"""Correct qualification using existing immutable camera captures; no redecoding.

Creates a new evidence version. The first attempt and its rejection remain
unchanged. Does not relax the original correlation or stability thresholds.
"""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess

from audit_moon import digest
from run_photo_pilot import save,checked_output
from qualify_photo_pairs import prepare


def run(source,output,trainer):
    root,output=checked_output(output)
    _,source=checked_output(source)
    freeze=json.loads((source/'freeze.json').read_text(encoding='utf-8'))
    records=json.loads((source/'captures.json').read_text(encoding='utf-8'))
    if records['freeze_sha256']!=digest(source/'freeze.json'):
        raise ValueError('Original extraction freeze changed')
    now=datetime.now(timezone.utc)
    freeze.update(campaign_id='neural-photo-pilot-v002-'+now.strftime('%Y%m%dT%H%M%SZ'),
        data_version=2,measured_at_utc=now.isoformat(),
        supersedes_campaign_id=freeze['campaign_id'],
        parent_freeze_sha256=digest(source/'freeze.json'),
        parent_captures_sha256=digest(source/'captures.json'),
        correction='Camera values already clip at 1 after WB: do not divide saturation masks by gains. Mask clipped highlights in registration. No threshold relaxation.',
        qualifier_revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip())
    freeze['qualification'].pop('unsaturated_sensor_fraction')
    freeze['qualification']['unsaturated_camera_fraction']=.9
    output.mkdir(parents=True,exist_ok=False)
    save(output/'freeze.json',freeze)
    result=prepare(output,freeze,records['records'])
    if result['status']=='qualified_for_exploratory_pilot':
        with (output/'training.log').open('x',encoding='utf-8') as log:
            subprocess.run([str(trainer.resolve()),str(root/'tests/neural/train_photo_pilot.py'),
                '--work',str(output)],cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=1800,check=True)
    print('COMPLETE '+str(output),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','output','trainer'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();run(a.source,a.output,a.trainer)
