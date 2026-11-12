"""Qualify 42 signed-camera burst frames with disjoint same-ISO reference/source sets.

Odd chronological positions form a 21-frame reference, remaining positions are
noisy sources. No source enters its reference, no radiometric correction, no
training, RAW copying or selection using neural-model performance.
"""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from audit_moon import digest
from qualify_control_scene import extract
from qualify_photo_pairs import prepare
from run_photo_pilot import checked_output,save


def split_positions(entries):
    if len(entries)!=42 or len({e['source']['destination_sha256'] for e in entries})!=42:
        raise ValueError('Need 42 distinct fixed burst frames')
    refs=entries[::2];sources=entries[1::2]
    if {r['source']['destination_sha256'] for r in refs}&{r['source']['destination_sha256'] for r in sources}:
        raise ValueError('A source entered its reference')
    return refs,sources


def run(args):
    root,work=checked_output(args.output)
    shared=root.parents[1] if root.parent.name=='.worktrees' else root
    folder=shared/'tests/image/colorimétrie/rafale'
    inventory=json.loads(args.inventory.read_text(encoding='utf-8'))
    records={str(Path(r['path']).resolve()).casefold():r for r in inventory['records']}
    entries=[]
    for number in range(9819,9861):
        path=folder/f'6V1A{number:04d}.CR3';r=records[str(path.resolve()).casefold()]
        m=r['metadata'];stat=path.stat()
        if ((stat.st_size,stat.st_mtime_ns)!=(r['bytes'],r['mtime_ns']) or
            (m['model'],m['iso'],m['exposure_s'],m['aperture'],m['focal_mm'])!=
                ('Canon EOS R6 Mark III',4000,.005,5.6,26.)):
            raise ValueError('Source or acquisition settings changed')
        entries.append(dict(group=1,iso=4000,scene='burst_4000_training_candidate',
            source=dict(destination=str(path.resolve()),destination_sha256=digest(path)),metadata=r))
    references,sources=split_positions(entries)
    if shutil.disk_usage(root).free<4*2**30:raise ValueError('Need 4 GiB reserve')
    baseline=json.loads(args.baseline.read_text(encoding='utf-8'))
    now=datetime.now(timezone.utc)
    campaign='neural-same-iso-burst-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    identities={name:digest(getattr(args,name)) for name in ('tool','profile','manifest')}
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        inventory=dict(path=str(args.inventory.resolve()),sha256=digest(args.inventory)),
        baseline=dict(path=str(args.baseline.resolve()),sha256=digest(args.baseline)),
        identities=identities,dependency_paths={name:str(getattr(args,name).resolve()) for name in identities},
        qualification=baseline['freeze']['qualification'],
        scene_partition=dict(burst_4000_training_candidate='train'),
        reference_source_hashes=[e['source']['destination_sha256'] for e in references],
        noisy_source_hashes=[e['source']['destination_sha256'] for e in sources],
        split='21 alternating reference captures and 21 disjoint sources; chronological order frozen',
        radiometry_policy='identity_same_iso',
        reference_admission='Existing geometric gates, at least 8 registered references; all exclusions recorded',
        sources=entries,tools={name:digest(Path(__file__).with_name(name)) for name in
            ('qualify_same_iso_burst.py','qualify_control_scene.py','qualify_photo_pairs.py','photo_pair_math.py')})
    work.mkdir(parents=True,exist_ok=False);save(work/'freeze.json',freeze)
    env=dict(os.environ);env['OMP_NUM_THREADS']='2';env['OPENBLAS_NUM_THREADS']='2'
    for location in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(location).is_dir():env['PATH']=location+os.pathsep+env.get('PATH','')
    captures=[];started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(work/'freeze.json'),captures=captures,status='incomplete',training=None,
        limits=['Same-ISO means are noisy, may retain fixed-pattern noise and are not perfectly clean truth.',
                'File-disjoint references do not prove temporal noise independence or immobile content.',
                'Single candidate composition; no new independent validation/test partition.',
                'No neural fitting, reference swap, radiometric compensation or production change.'])
    try:
        for entry in entries:
            captures.append(extract(entry,work,args.tool,args.profile,args.manifest,env,identities))
            print(f'CAPTURE {len(captures)}/42 {Path(captures[-1]["source"]).name}',flush=True)
        save(work/'captures.json',dict(freeze_sha256=digest(work/'freeze.json'),records=captures))
        if any(digest(getattr(args,name))!=value for name,value in identities.items()):
            raise ValueError('Capture dependency changed')
        result['qualification']=prepare(work,freeze,captures)
        result['status']=('candidate_training_pairs_available' if result['qualification']['split_counts']['train']>=
            freeze['qualification']['minimum_training_patches'] else 'insufficient_candidate_pairs')
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(work/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print('COMPLETE '+result['status'],flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','baseline','tool','profile','manifest','output'):
        p.add_argument('--'+name,type=Path,required=True)
    run(p.parse_args())
