"""Inventory French/English counterparts; hashes track source drift, not translation quality."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
MARKER=re.compile(r'translation-source:\s*([^;\r\n]+);\s*sha256:\s*([a-f0-9]{64})')


def counterpart(path):
    return path.with_name(path.stem+'_en'+path.suffix)


def source_hash(path):
    # Universal newlines avoid false drift after a Windows/Linux Git checkout.
    return hashlib.sha256(path.read_text(encoding='utf-8-sig').encode('utf-8')).hexdigest()


def record(root,path):
    source=root/path;english=counterpart(source)
    digest=source_hash(source)
    result=dict(source=path.as_posix(),source_sha256=digest,bytes=source.stat().st_size,
                english=english.relative_to(root).as_posix())
    if path.as_posix().startswith(('docs/reports/archive/','docs/reports/sessions/','docs/reports/storage/')):
        result['scope']='historical_record'
    elif path.as_posix().startswith('docs/validation/analyses/'):
        result['scope']='analysis_report'
    elif path.as_posix().startswith('docs/scholar/'):
        result['scope']='learning_material'
    else:result['scope']='live_reference'
    if not english.exists():result['status']='missing'
    else:
        marker=MARKER.search(english.read_text(encoding='utf-8-sig'))
        if marker is None:result['status']='present_unverified'
        elif marker[1].strip()!=path.as_posix():result['status']='wrong_source'
        elif marker[2]!=digest:result['status']='source_changed'
        else:result['status']='source_hash_matches'
    return result


def inventory(root,paths):
    records=[]
    for path in sorted(set(paths)):
        if path.suffix not in ('.md','.tex') or path.stem.endswith('_en'):continue
        if path.name=='AGENTS.md':continue
        if any(part in ('build','__pycache__') for part in path.parts):continue
        if not (root/path).is_file():continue
        records.append(record(root,path))
    counts={status:sum(r['status']==status for r in records) for status in
            ('missing','present_unverified','source_changed','wrong_source','source_hash_matches')}
    scopes={scope:{status:sum(r['scope']==scope and r['status']==status for r in records) for status in counts}
            for scope in ('live_reference','learning_material','analysis_report','historical_record')}
    return dict(schema_version=1,created_utc=datetime.now(timezone.utc).isoformat(),counts=counts,scopes=scopes,records=records,
        source_hash_policy='SHA-256 of UTF-8 text without BOM, with LF-normalized newlines; not a binary evidence hash.',
        limits=['Filename pairing is a convention, not language detection.',
                'A matching source hash does not prove translation accuracy, current production behavior, link anchors or PDF compilation.',
                'Historical reports keep their dates and verdicts; JSON analysis evidence is not translated.',
                'AGENTS.md instructions and generated build trees are excluded, not declared translated.'])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path)
    args=p.parse_args()
    paths=[Path(s) for s in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard'],cwd=ROOT,text=True,encoding='utf-8').splitlines()]
    result=inventory(ROOT,paths);result['revision']=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if args.output:
        destination=args.output.resolve()
        if not destination.is_relative_to((ROOT/'docs/reports').resolve()):raise ValueError('Audit output must stay under docs/reports')
        with destination.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2)
    print(json.dumps(result['counts'],ensure_ascii=False))
    return 0


if __name__=='__main__':raise SystemExit(main())
