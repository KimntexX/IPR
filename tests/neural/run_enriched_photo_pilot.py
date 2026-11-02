"""One fixed 2048-step pilot: existing train plus disjoint ISO4000 burst pairs."""
import argparse
from copy import deepcopy
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

from audit_moon import digest
from qualify_photo_pairs import prepare,declared_scenes
from run_photo_pilot import checked_output,save
from run_reserved_photo_pilot import select_inputs,verify_engines,validation_identities,coverage


def example_identities(examples):
    return sorted((e['id'],e['source_sha256'],e['content_sha256'],e['target_sha256']) for e in examples)


def combine(captures,roles,burst):
    if burst['status']!='candidate_training_pairs_available':raise ValueError('Unqualified burst')
    combined=captures+deepcopy(burst['captures'])
    roles=dict(roles,burst_4000_training_candidate='train')
    declared_scenes(dict(scene_partition=roles),combined)
    for field in ('model','profile_sha256','manifest_sha256','domain'):
        if len({c[field] for c in combined})!=1:raise ValueError('Capture contract mismatch: '+field)
    if not all(c['calibration_applied'] for c in combined):raise ValueError('Calibration required')
    return combined,roles


def run(args):
    root,work=checked_output(args.output)
    parents={n:json.loads(getattr(args,n).read_text(encoding='utf-8')) for n in
        ('training_parent','control_parent','burst_parent','previous_pilot')}
    old,control,burst,previous=(parents[n] for n in ('training_parent','control_parent','burst_parent','previous_pilot'))
    captures,roles,protocol=select_inputs(old,control)
    captures,roles=combine(captures,roles,burst)
    compatibility=verify_engines(root,captures,old,control)
    if protocol!=burst['freeze']['qualification']:raise ValueError('Qualification protocol mismatch')
    training=deepcopy(previous['freeze']['training']);training['steps']=2048
    now=datetime.now(timezone.utc)
    campaign='neural-enriched-photo-pilot-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={n:dict(path=str(getattr(args,n).resolve()),sha256=digest(getattr(args,n))) for n in parents},
        scene_partition=roles,qualification=protocol,training=training,
        scene_radiometry=dict(burst_4000_training_candidate='identity_same_iso'),
        reference_source_hashes=[c['source_sha256'] for c in captures if c['iso']==100]+
            burst['freeze']['reference_source_hashes'],
        capture_source_compatibility=compatibility,sources=captures,
        model_selection='Single seed, width, optimizer and final 2048-step checkpoint; no control-dependent selection.',
        tools={n:digest(Path(__file__).with_name(n)) for n in
            ('run_enriched_photo_pilot.py','run_reserved_photo_pilot.py','qualify_photo_pairs.py',
             'photo_pair_math.py','train_photo_pilot.py','camera_data.py','camera_unet.py')})
    work.mkdir(parents=True,exist_ok=False);save(work/'freeze.json',freeze)
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(work/'freeze.json'),status='incomplete',training=None,
        limits=['Previously examined control remains outside training but is not an independent final test.',
            '342 old plus 152 burst patches are correlated observations of two training compositions.',
            'ISO100 isolated content supports excluded; no synthetic-noise or clean-target assumption.',
            'Reference means retain noise; any gain requires structure/colour checks and later independent scenes.',
            'Data expansion and fixed 2048-step budget are exploratory, no causal attribution to architecture.'])
    started=time.monotonic()
    try:
        pairs=prepare(work,freeze,captures);result['qualification']=pairs
        if validation_identities(pairs)!=validation_identities(previous['qualification']):
            raise ValueError('Control source/target identities changed')
        expected=previous['qualification']['examples']+burst['qualification']['examples']
        if example_identities(pairs['examples'])!=example_identities(expected):
            raise ValueError('Previously qualified examples changed')
        result['identity_check']='all original and burst source/target hashes unchanged'
        result['iso_coverage']=coverage(pairs['examples'])
        if pairs['status']!='qualified_for_exploratory_pilot':raise ValueError('Insufficient combined data')
        print('START fixed 2048-step pilot '+json.dumps(result['iso_coverage']),flush=True)
        env=dict(os.environ,OMP_NUM_THREADS='4',OPENBLAS_NUM_THREADS='2')
        with (work/'training.log').open('x',encoding='utf-8') as log:
            subprocess.run([str(args.trainer.resolve()),str(root/'tests/neural/train_photo_pilot.py'),
                '--work',str(work)],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
        result['training']=json.loads((work/'training.json').read_text(encoding='utf-8'))
        result['training_sha256']=digest(work/'training.json')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):
            raise ValueError('Study code changed during execution')
        result['status']=result['training']['verdict']
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(work/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+result['status'],flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('training-parent','control-parent','burst-parent','previous-pilot','trainer','output'):
        p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())
