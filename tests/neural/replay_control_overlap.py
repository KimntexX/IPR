"""Replay the fixed primary control under an explicit common-support protocol.

No decoding, primary-reference substitution, threshold search or training.
The old protocol's rejection remains canonical, not silently corrected away.
"""
import argparse
from copy import deepcopy
from datetime import datetime,timezone
import json
from pathlib import Path
import subprocess
import time

from audit_moon import digest
from qualify_control_scene import reference_variant
from qualify_photo_pairs import prepare,overlap_protocol
from run_photo_pilot import checked_output,save


def run(source,output):
    root,output=checked_output(output)
    parent=json.loads(source.read_text(encoding='utf-8'))
    if parent.get('error') or len(parent['captures'])!=29:
        raise ValueError('Completed immutable control captures required')
    now=datetime.now(timezone.utc)
    campaign='neural-control-overlap-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=deepcopy(parent['freeze'])
    freeze.update(campaign_id=campaign,data_version=1,measured_at_utc=now.isoformat(),
        parent_campaign=parent['campaign_id'],
        parent_result=dict(path=str(source.resolve()),sha256=digest(source)),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        protocol_revision_reason='Replace fixed 12px gate with geometric support and reciprocity; do not relax ECC, stability, local support, radiometry, monitoring or patch masks.',
        qualification=overlap_protocol(freeze['qualification']),
        reference_group=1,role='primary_control',
        tools={name:digest(Path(__file__).with_name(name)) for name in
               ('replay_control_overlap.py','photo_pair_math.py','qualify_photo_pairs.py')})
    captures=reference_variant(parent['captures'],1)
    output.mkdir(parents=True,exist_ok=False)
    save(output/'freeze.json',freeze)
    started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,
        measured_at_utc=now.isoformat(),freeze=freeze,freeze_sha256=digest(output/'freeze.json'),
        captures=captures,status='incomplete',training=None,
        limits=['Only the predetermined first ISO100 reference is used.',
                'Protocol changed explicitly; outcomes are not comparable as an unchanged-gate replay.',
                'Global overlap is not sufficient: per-tile 50% support, local quality and patch 90% masks remain.',
                'No learned output used for admission; no neural training or product quality validation.'])
    try:
        pairs=prepare(output,freeze,captures)
        result['qualification']=pairs
        result['status']=('primary_control_pairs_available' if pairs['split_counts']['validation']>=
            freeze['qualification']['minimum_validation_patches'] else 'insufficient_primary_control_pairs')
    except Exception as error:
        result['error']=str(error)
        raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result)
        save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+result['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();run(args.source,args.output)
