"""Read-only corpus bridge from historical noise studies; preserve all frozen splits."""
import argparse
from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path, PureWindowsPath
import subprocess
from audit_dark_mean import sha256


def choose_pilot(records):
    chosen=[]
    for category,iso in (('burst42',4000),('confirmed_dark',1600),('confirmed_dark',51200)):
        candidates=[r for r in records if r['category']==category and r['split']=='development'
                    and r['metadata']['iso']==iso and r['status']=='available_hash_match']
        chosen.extend(candidates[:2])
    return chosen


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--project-root',type=Path,required=True)
    p.add_argument('--history-root',type=Path,required=True)
    args=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    historical_head=subprocess.check_output(['git','rev-parse','refs/heads/patch_noise'],cwd=root,text=True).strip()
    history=args.history_root/'docs/validation/raw'
    names=['partition-rafale-bruit-v001-20261027T225326Z.json',
           'qualification-noirs-repetes-bruit-v001-20261028T100350Z.json',
           'gel-rafales-carte-bruit-v001-20261028T094743Z.json',
           'qualification-rafales-carte-bruit-v001-20261028T094754Z.json']
    parents={name:json.loads((history/name).read_text(encoding='utf-8')) for name in names}
    records=[]
    for identity in parents[names[0]]['identities']:
        records.append(dict(category='burst42',split=identity['split'],
            original_path=identity['path'],sha256=identity['sha256'],metadata=identity['metadata']))
    for series in parents[names[1]]['records']:
        for i,frame in enumerate(series['decoded_frames']):
            if i in series['development_indices']:
                split='development'
            elif i in series['control_indices']:
                split='historical_control_already_measured'
            else:
                raise ValueError('Black frame outside the frozen partition')
            records.append(dict(category='confirmed_dark',split=split,original_path=frame['path'],
                sha256=frame['sha256'],metadata=frame['metadata']))
    for frame in parents[names[2]]['frames']:
        records.append(dict(category='illuminated_dark_room',split='historical_group_already_measured',
            original_path=frame['path'],sha256=frame['sha256'],metadata=frame['metadata']))
    roots=[args.project_root/'tests/image/colorimétrie/rafale',
           args.project_root/'tests/image/colorimétrie/protocole/blacks',
           Path('D:/DCIM/100EOSR6'),Path('E:/DCIM/100EOSR6')]
    cache={}
    for record in records:
        basename=PureWindowsPath(record['original_path']).name
        candidates=[Path(record['original_path']),*(folder/basename for folder in roots)]
        seen=set();attempts=[]
        for path in candidates:
            if path in seen or not path.is_file():
                continue
            seen.add(path)
            if path not in cache:
                cache[path]=sha256(path)
            attempts.append(dict(path=str(path),sha256=cache[path]))
            if cache[path]==record['sha256']:
                record.update(status='available_hash_match',path=str(path),bytes=path.stat().st_size)
                break
        else:
            record['status']='available_but_hash_mismatch' if attempts else 'missing'
        record['attempts']=attempts
    known={PureWindowsPath(r['original_path']).name for r in records}
    unclassified=[]
    for folder in roots[:2]:
        for path in sorted(folder.glob('*.CR3')):
            if path.name not in known:
                unclassified.append(dict(path=str(path),bytes=path.stat().st_size,
                    status='not_in_selected_historical_parents_no_pixel_analysis'))
    pilot=choose_pilot(records)
    if any(r['split']!='development' for r in pilot):
        raise ValueError('Pilot touches a nondevelopment split')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    proof=dict(schema_version=1,created_utc=stamp,study='cfa_corpus_bridge_inventory',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        historical_branch='patch_noise',historical_head=historical_head,
        parents={name:dict(path=str(history/name),sha256=sha256(history/name)) for name in names},
        source=dict(sha256=sha256(__file__),text=Path(__file__).read_text(encoding='utf-8')),
        records=records,unclassified_files=unclassified,
        counts=dict(Counter(f"{r['category']}:{r['split']}:{r['status']}" for r in records)),
        pilot=[dict(path=r['path'],sha256=r['sha256'],category=r['category'],split=r['split'],metadata=r['metadata']) for r in pilot],
        limitations=['Only filesystem and SHA256 checks; no RAW pixels decoded here, sealed frames hash-only.',
                     'Metadata inherited from authenticated historical proofs, not re-extracted.',
                     'Sealed burst frames 9847-9860 remain sealed; old validation/control is not newly independent.',
                     'Dark-room scenes are illuminated, NOT lens-cap black frames.',
                     'Unclassified files are not assigned a role from the directory name alone.',
                     'No coverage claim for other cameras, ISO or C-RAW variants.'])
    output=root/'docs/validation/raw'/f'cfa-corpus-inventory-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output)
    print(json.dumps(proof['counts'],ensure_ascii=False))
    print('Unclassified',len(unclassified),'pilot',[Path(r['path']).name for r in pilot])


if __name__=='__main__':
    main()
