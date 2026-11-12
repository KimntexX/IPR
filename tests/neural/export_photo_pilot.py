"""Preserve complete photo-pilot evidence in an immutable canonical JSON."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from audit_moon import digest


def export(work,captures_work,initial_invalid=False):
    root=Path(__file__).resolve().parents[2]
    freeze=json.loads((work/'freeze.json').read_text(encoding='utf-8'))
    pairs=json.loads((work/'pairs.json').read_text(encoding='utf-8'))
    captures=json.loads((captures_work/'captures.json').read_text(encoding='utf-8'))
    extraction_freeze=json.loads((captures_work/'freeze.json').read_text(encoding='utf-8'))
    if (pairs['freeze_sha256']!=digest(work/'freeze.json') or
        captures['freeze_sha256']!=digest(captures_work/'freeze.json')):
        raise ValueError('Changed frozen evidence')
    if len(captures['records'])!=69 or len({c['source_sha256'] for c in captures['records']})!=69:
        raise ValueError('Unexpected source coverage')
    source=subprocess.check_output(['git','show','bd1fced:tests/neural/capture_camera.cpp'],cwd=root)
    training_path=work/'training.json'
    result=dict(schema_version=1,data_version=freeze['data_version'],campaign_id=freeze['campaign_id'],
        measured_at_utc=pairs['measured_at_utc'],freeze=freeze,extraction_freeze=extraction_freeze,
        extraction=captures,qualification=pairs,
        extractor_source_revision='bd1fced',extractor_cpp_sha256=hashlib.sha256(source).hexdigest(),
        export_tool_sha256=digest(__file__),
        training=json.loads(training_path.read_text(encoding='utf-8')) if training_path.exists() else None,
        training_status='completed' if training_path.exists() else 'not_started_insufficient_qualified_validation_pairs',
        verdict='invalid_initial_saturation_gate_no_training' if initial_invalid else pairs['status'],
        limits=pairs['limits'])
    if initial_invalid:
        result['invalidity']='Saturation incorrectly divided by WB gains, although IPR bounds balanced CFA channels at white1. Initial conclusions superseded by data_version2.'
    elif freeze.get('supersedes_campaign_id'):
        result['supersedes_campaign_id']=freeze['supersedes_campaign_id']
    stamp=freeze['campaign_id'].split('-')[-1]
    destination=root/'docs/validation/raw'/f'neural-photo-pilot-v{freeze["data_version"]:03d}-{stamp}.json'
    with destination.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,indent=2,allow_nan=False)
    print(str(destination),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--captures-work',type=Path,required=True)
    parser.add_argument('--initial-invalid',action='store_true')
    args=parser.parse_args();export(args.work,args.captures_work,args.initial_invalid)
