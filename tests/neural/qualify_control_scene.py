"""Qualify an independent control composition, with two unpooled ISO100 bursts.

Primary reference is fixed chronologically before measurement. The later burst
is diagnostic only, never a substitute chosen after observing admission counts.
No training, TIFFs, source modification or Git publication.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from audit_moon import digest
from qualify_photo_pairs import prepare,overlap_protocol
from run_photo_pilot import checked_output, save


GROUPS=((1,100,301,309),(2,1600,310,319),(3,100,320,327),
        (4,3200,328,339),(5,6400,340,351),(6,12800,352,366))


def selected_sources(records):
    by_name={Path(r['destination']).stem:r for r in records}
    if len(by_name)!=len(records):
        raise ValueError('Duplicate imported names')
    selected=[]
    for group,iso,first,last in GROUPS:
        indices=range(first,last+1) if iso==100 else sorted({first,(first+last)//2,last})
        for number in indices:
            name=f'6V1A{number:04d}'
            if name not in by_name:
                raise ValueError('Missing fixed control source '+name)
            selected.append(dict(group=group,iso=iso,source=by_name[name]))
    return selected


def reference_variant(captures,group):
    if group not in (1,3):
        raise ValueError('Expected one of the two separate reference bursts')
    result=[c for c in captures if c['iso']!=100 or c['group']==group]
    if len([c for c in result if c['iso']==100])!=(9 if group==1 else 8):
        raise ValueError('Incomplete reference burst; no implicit frame exclusion')
    return result


def extract(entry,work,tool,profile,manifest,env,identities):
    source=Path(entry['source']['destination'])
    if digest(source)!=entry['source']['destination_sha256']:
        raise ValueError('Imported source changed: '+str(source))
    descriptor=work/(source.stem+'.json')
    with (work/(source.stem+'.log')).open('x',encoding='utf-8') as log:
        subprocess.run([str(tool.resolve()),'--input',str(source),'--output',str(descriptor),
            '--profile',str(profile.resolve()),'--manifest',str(manifest.resolve())],
            env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
    captured=json.loads(descriptor.read_text(encoding='utf-8'))
    if (captured['source_sha256']!=entry['source']['destination_sha256'] or
            captured['profile_sha256']!=identities['profile'] or
            captured['manifest_sha256']!=identities['manifest'] or
            not captured['calibration_applied'] or captured['iso']!=entry['iso']):
        raise ValueError('Extraction identity, ISO or calibration mismatch')
    captured.update(group=entry['group'],scene=entry.get('scene','control_indoor'),descriptor=str(descriptor),
                    descriptor_sha256=digest(descriptor))
    return captured


def qualify_variant(work,freeze,captures,group):
    output=work/('primary-early' if group==1 else 'diagnostic-late')
    output.mkdir(exist_ok=False)
    variant=deepcopy(freeze)
    variant.update(reference_group=group,
                   role='primary_control' if group==1 else 'diagnostic_not_a_replacement',
                   campaign_id=freeze['campaign_id']+f'-reference-{group}')
    save(output/'freeze.json',variant)
    pairs=prepare(output,variant,reference_variant(captures,group))
    return dict(freeze=variant,freeze_sha256=digest(output/'freeze.json'),
                emitted_pairs_sha256=digest(output/'pairs.json'),pairs=pairs)


def run(args):
    root,work=checked_output(args.output)
    imported=json.loads(args.inventory.read_text(encoding='utf-8'))
    baseline=json.loads(args.baseline.read_text(encoding='utf-8'))
    if imported['status']!='copied_and_hash_verified' or imported['partition']!='validation':
        raise ValueError('Expected a verified, validation-reserved import')
    selected=selected_sources(imported['records'])
    for path in (args.tool,args.profile,args.manifest):
        if not path.is_file(): raise ValueError('Missing dependency: '+str(path))
    if shutil.disk_usage(root).free<4*2**30:
        raise ValueError('Need 4 GiB reserve for this bounded extraction')
    now=datetime.now(timezone.utc)
    campaign='neural-control-pairs-v002-'+now.strftime('%Y%m%dT%H%M%SZ')
    identities={name:digest(getattr(args,name)) for name in ('tool','profile','manifest')}
    freeze=dict(schema_version=1,data_version=2,campaign_id=campaign,
        measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        tool_sources={name:digest(Path(__file__).with_name(name)) for name in
            ('qualify_control_scene.py','qualify_photo_pairs.py','photo_pair_math.py','capture_camera.cpp')},
        imported_inventory=dict(path=str(args.inventory.resolve()),sha256=digest(args.inventory)),
        baseline=dict(path=str(args.baseline.resolve()),sha256=digest(args.baseline)),
        identities=identities,
        dependency_paths={name:str(getattr(args,name).resolve()) for name in identities},
        qualification=overlap_protocol(baseline['qualification']),
        scene_partition=dict(control_indoor='validation'),
        primary_reference='first chronological ISO100 burst: 0301..0309; fixed before measurement',
        diagnostic_reference='second ISO100 burst: 0320..0327; never pool or choose by admission score',
        selection='All 17 ISO100 frames; first/middle/last from each of four higher-ISO bursts.',
        sources=selected,training=None)
    if freeze['qualification'].get('unsaturated_camera_fraction')!=.9:
        raise ValueError('Need the corrected, camera-axis baseline protocol')
    work.mkdir(parents=True,exist_ok=False)
    save(work/'freeze.json',freeze)
    env=dict(os.environ)
    env['OMP_NUM_THREADS']='2';env['OPENBLAS_NUM_THREADS']='2'
    for folder in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin',
                   'C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(folder).is_dir(): env['PATH']=folder+os.pathsep+env.get('PATH','')
    started=time.monotonic();captures=[];variants=[]
    result=dict(schema='ipr.neural.control-pairs.v1',schema_version=1,data_version=2,
        campaign_id=campaign,measured_at_utc=now.isoformat(),freeze=freeze,
        freeze_sha256=digest(work/'freeze.json'),captures=captures,variants=variants,
        status='incomplete',training=None,
        limits=['Camera-pair qualification only, no neural training or quality claim.',
                'Related materials and one camera, not independent product qualification.',
                'ISO100 references remain noisy and may retain fixed-pattern noise.',
                'Later reference is a sensitivity diagnostic; no post-hoc replacement.'])
    try:
        for entry in selected:
            captures.append(extract(entry,work,args.tool,args.profile,args.manifest,env,identities))
            print(f'CAPTURE {len(captures)}/{len(selected)} {Path(captures[-1]["source"]).name}',flush=True)
        if any(digest(getattr(args,name))!=value for name,value in identities.items()):
            raise ValueError('Extraction dependency changed')
        save(work/'captures.json',dict(freeze_sha256=digest(work/'freeze.json'),records=captures))
        for group in (1,3):
            variants.append(qualify_variant(work,freeze,captures,group))
        counts=variants[0]['pairs']['split_counts']
        result['status']=('primary_control_pairs_available' if
            counts['validation']>=freeze['qualification']['minimum_validation_patches'] else
            'insufficient_primary_control_pairs_no_training')
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
    for name in ('inventory','baseline','tool','profile','manifest','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    run(parser.parse_args())
