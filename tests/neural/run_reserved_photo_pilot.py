"""Rebuild fixed train/control pairs, then run one frozen photographic pilot.

No decoding, control-reference substitution, hyperparameter search, production
changes or Git publication. Existing physical-scene partitions are preserved.
"""
import argparse
from collections import Counter
from copy import deepcopy
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

from audit_moon import digest
from qualify_photo_pairs import prepare,overlap_protocol,declared_scenes
from run_photo_pilot import checked_output,save


def select_inputs(training_parent,control_parent):
    if control_parent['status']!='primary_control_pairs_available':
        raise ValueError('Primary control was not qualified')
    if control_parent['freeze']['reference_group']!=1:
        raise ValueError('The diagnostic reference cannot replace the primary')
    train=[deepcopy(c) for c in training_parent['extraction']['records'] if c['scene']=='indoor']
    control=deepcopy(control_parent['captures'])
    if len(train)!=27 or len(control)!=21:
        raise ValueError('Unexpected frozen composition coverage')
    if any(c['scene']!='control_indoor' for c in control):
        raise ValueError('Unexpected control composition')
    captures=train+control
    roles=dict(indoor='train',control_indoor='validation')
    declared_scenes(dict(scene_partition=roles),captures)
    for field in ('model','profile_sha256','manifest_sha256','domain'):
        if len({c[field] for c in captures})!=1:
            raise ValueError('Mismatched capture contract: '+field)
    if not all(c['calibration_applied'] for c in captures):
        raise ValueError('Calibration must actually have applied to every capture')
    protocol=overlap_protocol(training_parent['freeze']['qualification'])
    if protocol!=control_parent['freeze']['qualification']:
        raise ValueError('Training and control qualification protocols differ')
    return captures,roles,protocol


def validation_identities(pairs):
    return sorted((r['id'],r['source_sha256'],r['content_sha256'],r['target_sha256'])
                  for r in pairs['examples'] if r['split']=='validation')


def coverage(examples):
    return {role:{str(iso):count for iso,count in sorted(Counter(
        r['iso'] for r in examples if r['split']==role).items())}
        for role in ('train','validation','test')}


def extractor_body(source):
    """Only the two reviewed include-only cleanup lines may differ."""
    allowed={'#include <algorithm>','#include <QCommandLineOption>'}
    return '\n'.join(line for line in source.decode('utf-8').splitlines() if line not in allowed)


def verify_engines(root,captures,training_parent,control_parent):
    revisions=sorted({c['engine_revision'] for c in captures})
    for revision in revisions[1:]:
        # The embedded stamp describes the configured engine, not necessarily
        # the later commit in which the bench extractor was first added.
        subprocess.run(['git','diff','--exit-code',revisions[0],revision,'--','src',
            'config/raw-normalization.json'],
            cwd=root,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=30)
    old_source=subprocess.check_output(['git','show',training_parent['extractor_source_revision']+
                                      ':tests/neural/capture_camera.cpp'],cwd=root,timeout=30)
    import hashlib
    if hashlib.sha256(old_source).hexdigest()!=training_parent['extractor_cpp_sha256']:
        raise ValueError('Recorded training extractor source hash mismatch')
    new_source=(root/'tests/neural/capture_camera.cpp').read_bytes()
    if digest(root/'tests/neural/capture_camera.cpp')!=control_parent['freeze']['tool_sources']['capture_camera.cpp']:
        raise ValueError('Control extractor source changed since acquisition')
    if extractor_body(old_source)!=extractor_body(new_source):
        raise ValueError('Extraction calculations differ; captures cannot be pooled')
    return dict(revisions=revisions,production_source_diff='empty',
        training_extractor_revision=training_parent['extractor_source_revision'],
        training_extractor_sha256=training_parent['extractor_cpp_sha256'],
        control_extractor_sha256=digest(root/'tests/neural/capture_camera.cpp'),
        extractor_calculation_diff='empty after the reviewed QCommandLineOption/algorithm include-only cleanup')


def run(args):
    root,work=checked_output(args.output)
    old=json.loads(args.training_parent.read_text(encoding='utf-8'))
    control=json.loads(args.control_parent.read_text(encoding='utf-8'))
    captures,roles,protocol=select_inputs(old,control)
    compatibility=verify_engines(root,captures,old,control)
    if not args.trainer.is_file():raise ValueError('Missing isolated PyTorch interpreter')
    now=datetime.now(timezone.utc)
    campaign='neural-reserved-photo-pilot-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,
        measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={name:dict(path=str(getattr(args,name).resolve()),sha256=digest(getattr(args,name)))
                 for name in ('training_parent','control_parent')},
        scene_partition=roles,qualification=protocol,training=deepcopy(old['freeze']['training']),
        capture_source_compatibility=compatibility,
        sources=captures,
        tools={name:digest(Path(__file__).with_name(name)) for name in
            ('run_reserved_photo_pilot.py','qualify_photo_pairs.py','photo_pair_math.py',
             'train_photo_pilot.py','camera_unet.py','camera_data.py')},
        validation_expected_identity_count=len(validation_identities(control['qualification'])),
        primary_reference='0301..0309 only; diagnostic burst never substituted',
        model_selection='One fixed seed/configuration/final checkpoint; no validation-based tuning')
    work.mkdir(parents=True,exist_ok=False)
    save(work/'freeze.json',freeze)
    started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(work/'freeze.json'),status='incomplete',training=None,
        limits=['Exploratory two-composition pilot; no independent third scene or camera.',
                'Training and validation may cover different ISOs; coverage is reported explicitly.',
                'Patch counts are correlated samples, not independent scenes.',
                'Reference averages are not perfectly clean sensor ground truth.',
                'No display rendering, visual validation or production integration.'])
    try:
        pairs=prepare(work,freeze,captures)
        result['qualification']=pairs
        if validation_identities(pairs)!=validation_identities(control['qualification']):
            raise ValueError('The reserved control examples changed while assembling training')
        result['validation_identity_check']='all reserved source/target hashes unchanged'
        result['iso_coverage']=coverage(pairs['examples'])
        if pairs['status']!='qualified_for_exploratory_pilot':
            result['status']='insufficient_pairs_no_training'
            return
        print('ISO COVERAGE '+json.dumps(result['iso_coverage']),flush=True)
        print('START fixed 256-step pilot; no control-dependent settings',flush=True)
        env=dict(os.environ);env['OMP_NUM_THREADS']='4';env['OPENBLAS_NUM_THREADS']='2'
        with (work/'training.log').open('x',encoding='utf-8') as log:
            subprocess.run([str(args.trainer.resolve()),str(root/'tests/neural/train_photo_pilot.py'),
                '--work',str(work)],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,
                check=True,timeout=1800)
        result['training']=json.loads((work/'training.json').read_text(encoding='utf-8'))
        result['training_sha256']=digest(work/'training.json')
        if any(digest(Path(__file__).with_name(name))!=value for name,value in freeze['tools'].items()):
            raise ValueError('Pilot tool changed during execution')
        result['status']=result['training']['verdict']
    except Exception as error:
        result['error']=str(error)
        raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(work/'result.json',result)
        save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+result['status'],flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('training-parent','control-parent','trainer','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    run(parser.parse_args())
