"""Read-only local corpus audit; inventory is not permission or qualification to train.

Static CR3/CR2 metadata, bounded JPEG screening and existing tensor coverage.
No RAW development, source copying, model fitting or partition reassignment.
"""
import argparse
from collections import Counter
from datetime import datetime,timezone
import json
import re
from pathlib import Path
import subprocess
import time

import cv2
import numpy as np
from PIL import Image

from audit_moon import metadata,digest
from audit_capture_series import small_preview
from audit_pair_geometry import fit_geometry
from run_photo_pilot import checked_output,save


CAMERA='Canon EOS R6 Mark III'


def read_metadata(path):
    if path.suffix.lower()=='.cr3':return metadata(path)
    before=path.stat()
    with Image.open(path) as image:
        main=image.getexif();exif=main.get_ifd(34665)
        fields={name:str(main[tag]) for tag,name in ((271,'make'),(272,'model'),(306,'datetime')) if tag in main}
        for tag,name,convert in ((33434,'exposure_s',float),(33437,'aperture',float),
            (34855,'iso',int),(36867,'datetime_original',str),(37386,'focal_mm',float)):
            if tag in exif:fields[name]=convert(exif[tag])
    after=path.stat()
    if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
        raise ValueError('Source changed during metadata read')
    return dict(path=str(path.resolve()),bytes=before.st_size,mtime_ns=before.st_mtime_ns,
        metadata=fields,complete_basic=all(k in fields for k in ('model','iso','exposure_s','aperture')),
        metadata_parser='Pillow TIFF/EXIF, no pixel decoding')


def declared_role(record):
    path=Path(record['path']);parts={p.casefold() for p in path.parts};m=record['metadata']
    match=re.fullmatch(r'6V1A(\d{4})',path.stem,re.IGNORECASE)
    if m.get('model')==CAMERA and match and str(m.get('datetime_original','')).startswith('2026:11:01'):
        number=int(match[1])
        if 131<=number<=209:return 'existing_training_composition'
        if 210<=number<=300:return 'reserved_unqualified_vegetation'
        if 301<=number<=366:return 'reserved_evaluated_control'
    if 'protocole' in parts:return 'calibration_only_candidate'
    if any('échantillon' in p or 'sample' in p for p in parts):return 'usage_rights_need_confirmation'
    if not record['complete_basic']:return 'incomplete_metadata'
    if m.get('model')!=CAMERA:return 'separate_camera_qualification_needed'
    if any(p in ('moon','lune','eclipse') for p in parts):return 'specialized_astronomy_candidate'
    if 'rafale' in parts:return 'existing_burst_needs_neural_qualification'
    return 'unqualified_single_or_sequence'


def alias_key(record):
    m=record['metadata']
    return (Path(record['path']).name.casefold(),m.get('model'),m.get('datetime_original'))


def candidate_groups(records):
    eligible={'unqualified_single_or_sequence','existing_burst_needs_neural_qualification'}
    ordered=sorted((r for r in records if r['role'] in eligible),key=lambda r:
        (str(Path(r['path']).parent).casefold(),str(r['metadata'].get('datetime_original','')),r['path']))
    groups=[];current=[];previous=None
    for r in ordered:
        m=r['metadata'];match=re.search(r'(\d+)$',Path(r['path']).stem)
        try:stamp=datetime.strptime(m['datetime_original'],'%Y:%m:%d %H:%M:%S')
        except (KeyError,ValueError):continue
        if not match:continue
        key=(str(Path(r['path']).parent).casefold(),m.get('model'),m.get('iso'),
             m.get('exposure_s'),m.get('aperture'),m.get('focal_mm'))
        number=int(match[1])
        continuous=(previous is not None and key==previous[0] and number==previous[2]+1
                    and 0<=(stamp-previous[1]).total_seconds()<=max(5.,float(m['exposure_s'])+2.))
        if not continuous:
            if len(current)>=8:groups.append(current)
            current=[]
        current.append(r);previous=(key,stamp,number)
    if len(current)>=8:groups.append(current)
    return groups


def screen(group):
    selected=sorted({0,len(group)//2,len(group)-1});rows=[];reference=None
    for index in selected:
        r=group[index];path=Path(r['path'])
        rgb,size=small_preview(path)
        gray=cv2.cvtColor(np.asarray(rgb),cv2.COLOR_RGB2GRAY).astype(np.float32)/255
        if reference is None:reference=gray
        geometry=fit_geometry(reference,gray,[size[1],size[0],3],'translation',2.)
        now=path.stat()
        if (now.st_size,now.st_mtime_ns)!=(r['bytes'],r['mtime_ns']):
            raise ValueError('Source changed after metadata scan')
        rows.append(dict(path=r['path'],full_sha256=digest(path),jpeg_size_wh=size,
            encoded_gray_mean=float(gray.mean()),encoded_gray_std=float(gray.std()),
            near_black_fraction=float((gray<.01).mean()),geometry=geometry))
    return rows


def signal_coverage(work):
    pairs=json.loads((work/'pairs.json').read_text(encoding='utf-8'))
    if digest(work/'dataset.npz')!=pairs['dataset']['sha256']:raise ValueError('Changed pilot dataset')
    with np.load(work/'dataset.npz',allow_pickle=False) as data:
        sigma=data['sigma'];sample=data['signal'][:,:,::8,::8]
    result={}
    for role in ('train','validation'):
        ids=[i for i,r in enumerate(pairs['examples']) if r['split']==role]
        rows=[pairs['examples'][i] for i in ids]
        result[role]=dict(patches=len(ids),physical_composition_labels=sorted({r['scene_group'] for r in rows}),
            distinct_noisy_raws=len({r['source_sha256'] for r in rows}),
            iso_counts=dict(Counter(str(r['iso']) for r in rows)),
            haar_sigma_q10_q50_q90_rgb=np.quantile(sigma[ids],[.1,.5,.9],axis=0).tolist(),
            sampled_signal_q01_q50_q99_rgb=np.quantile(sample[ids].transpose(1,0,2,3).reshape(3,-1),[.01,.5,.99],axis=1).tolist())
    return dict(parent_pairs_sha256=digest(work/'pairs.json'),dataset_sha256=digest(work/'dataset.npz'),
        sample_policy='Signal every eighth camera pixel in each axis; sigma uses every stored patch value.',
        units='Signed balanced camera units; Haar sigma is texture/correlation affected, not physical sensor noise.',records=result)


def run(output):
    root,output=checked_output(output);shared=root.parents[1]
    roots=[shared/'tests/image/colorimétrie',shared/'tests/image/base',shared/'tests/image/base-hors-protocole',
           Path('C:/Users/victo/Pictures'),Path('D:/DCIM'),Path('E:/DCIM')]
    moon_source=root/'docs/validation/raw/moon-audit-v001-20261031T194552Z.json'
    moon_aliases={alias_key(r) for r in json.loads(moon_source.read_text(encoding='utf-8'))['records']}
    output.mkdir(parents=True,exist_ok=False);now=datetime.now(timezone.utc)
    campaign='neural-corpus-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        roots=[str(p) for p in roots],extensions=['.CR3','.CR2'],minimum_sequence_length=8,
        maximum_gap_seconds='max(5, exposure+2); same settings and consecutive filenames',
        maximum_preview_groups=9,selection='ISO4000 known burst first, then lowest ISO and lexical source order; one per possible frame-alias signature',
        tools={name:digest(Path(__file__).with_name(name)) for name in
               ('audit_existing_training_corpus.py','audit_moon.py','audit_capture_series.py','audit_pair_geometry.py')},
        known_moon_inventory=dict(path=str(moon_source),sha256=digest(moon_source)))
    save(output/'freeze.json',freeze)
    started=time.monotonic();records=[];errors=[];scans=[];seen=set()
    for folder in roots:
        if not folder.is_dir():scans.append(dict(root=str(folder),status='unavailable'));continue
        found=subprocess.run(['rg','--files','--no-ignore','-g','*.CR3','-g','*.CR2','-g','*.cr3','-g','*.cr2',str(folder)],
                             capture_output=True,text=True,encoding='utf-8',timeout=120)
        scans.append(dict(root=str(folder),scan_returncode=found.returncode,stderr=found.stderr))
        for entry in found.stdout.splitlines():
            path=Path(entry).resolve();key=str(path).casefold()
            if key in seen:continue
            seen.add(key)
            try:
                r=read_metadata(path);r['scanned_root']=str(folder);r['role']=declared_role(r)
                if alias_key(r) in moon_aliases:r['role']='specialized_astronomy_candidate'
                records.append(r)
            except Exception as error:errors.append(dict(path=str(path),error=str(error)))
        print(f'INVENTORY {folder} records={len(records)} errors={len(errors)}',flush=True)
    # Protect possible aliases too. A name/time match is conservative quarantine,
    # not a claim that two payloads are equal or a newly qualified physical scene.
    protected={alias_key(r):r['role'] for r in records if r['role'] in
        ('calibration_only_candidate','existing_training_composition','reserved_evaluated_control','reserved_unqualified_vegetation')}
    aliases=Counter(alias_key(r) for r in records)
    for r in records:
        r['possible_frame_alias_count']=aliases[alias_key(r)]
        if alias_key(r) in protected:r['role']=protected[alias_key(r)]
    groups=candidate_groups(records)
    groups.sort(key=lambda g:(0 if any('rafale' in Path(r['path']).parts for r in g) else 1,
                              g[0]['metadata']['iso'],g[0]['path']))
    screening=[];signatures=set()
    for group in groups:
        if len(screening)>=9:break
        signature=tuple(alias_key(group[i]) for i in sorted({0,len(group)//2,len(group)-1}))
        if signature in signatures:continue
        signatures.add(signature)
        record=dict(first=group[0]['path'],last=group[-1]['path'],count=len(group),iso=group[0]['metadata']['iso'])
        try:record['preview_screening']=screen(group)
        except Exception as error:record['screening_error']=str(error)
        screening.append(record);print(f'SCREEN {len(screening)} ISO{record["iso"]} count={record["count"]}',flush=True)
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),freeze=freeze,
        freeze_sha256=digest(output/'freeze.json'),scans=scans,records=records,errors=errors,
        summary=dict(files=len(records),roles=dict(Counter(r['role'] for r in records)),
            cameras=dict(Counter(r['metadata'].get('model','unknown') for r in records)),
            r6iii_iso_file_counts=dict(Counter(str(r['metadata'].get('iso')) for r in records if r['metadata'].get('model')==CAMERA)),
            candidate_sequences=len(groups),preview_groups=len(screening),
            possible_alias_file_records=sum(r['possible_frame_alias_count']>1 for r in records)),
        candidate_sequences=[dict(first=g[0]['path'],last=g[-1]['path'],count=len(g),
            iso=g[0]['metadata']['iso'],paths=[r['path'] for r in g]) for g in groups],preview_screening=screening,
        existing_pilot_signal_coverage=signal_coverage(root/'build-patch_neural_restoration/reserved-photo-pilot-v001'),
        elapsed_seconds=time.monotonic()-started,status='inventory_and_screening_only_no_new_training_qualification',
        versions=dict(numpy=np.__version__,opencv=cv2.__version__),
        limits=['File counts include physical copies; possible aliases are not full-file deduplication.',
            'Chronological/settings groups and JPEG correlation do not establish scene identity or RAW pair equivalence.',
            'Only first/middle/last of at most nine sequences have preview and full-file hashes; no RAW pixel development.',
            'ISO alone does not determine noise; exposure, light, gains, decoding and texture also matter.',
            'Calibration folder provenance is not proof of a cap-closed exposure or clean image target.',
            'Other camera RGB domains are not pooled with R6III; source usage rights and scene roles still need confirmation.',
            'No new train/validation assignment, copying, moving, deleting, model inference or publication.'])
    save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['summary']),flush=True);print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args().output)
