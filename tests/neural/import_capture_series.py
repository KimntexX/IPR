"""Copy an audited RAW series without overwriting; retain full source/copy hashes.

This is corpus acquisition, not a test-image generator or pair qualification.
The whole composition is reserved for validation, including every ISO variant.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import subprocess

from audit_moon import digest


def copy_verified(source, destination, expected_size, expected_mtime_ns):
    source, destination = Path(source), Path(destination)
    before = source.stat()
    if (before.st_size, before.st_mtime_ns) != (expected_size, expected_mtime_ns):
        raise ValueError('Source changed since audit: ' + str(source))
    if destination.exists():
        raise FileExistsError(destination)
    expected_hash = digest(source)
    # Exclusive creation is essential: an existing reference must never be replaced.
    with source.open('rb') as incoming, destination.open('xb') as outgoing:
        shutil.copyfileobj(incoming, outgoing, 1024*1024)
    after = source.stat()
    copied_hash = digest(destination)
    if ((after.st_size, after.st_mtime_ns) != (before.st_size, before.st_mtime_ns)
            or destination.stat().st_size != before.st_size
            or copied_hash != expected_hash):
        raise ValueError('Copy/source integrity failure; retained for inspection: ' + str(destination))
    return dict(source=str(source.resolve()), destination=str(destination.resolve()),
                bytes=before.st_size, source_mtime_ns=before.st_mtime_ns,
                source_sha256=expected_hash, destination_sha256=copied_hash)


def run(audit_path, destination, evidence):
    root = Path(__file__).resolve().parents[2]
    # Worktrees share the user's existing corpus, not a second worktree-local copy.
    shared = root.parents[1] if root.parent.name == '.worktrees' else root
    destination = destination.resolve()
    if not destination.is_relative_to((shared/'tests/image/colorimétrie').resolve()):
        raise ValueError('Destination must be inside the shared colorimetry corpus')
    if evidence.parent.resolve() != (root/'docs/validation/raw').resolve():
        raise ValueError('Evidence must be a new canonical analysis JSON')
    if evidence.exists() or destination.exists():
        raise FileExistsError('Use a new destination and evidence file; no overwrites')
    audit = json.loads(audit_path.read_text(encoding='utf-8'))
    records = audit['records']
    if not records or len({Path(r['path']).name for r in records}) != len(records):
        raise ValueError('Missing records or duplicate file names')
    if shutil.disk_usage(shared).free < sum(r['bytes'] for r in records) + 2*2**30:
        raise ValueError('Insufficient free space including 2 GiB reserve')
    destination.mkdir(parents=True, exist_ok=False)
    result = dict(schema='ipr.neural.corpus-import.v1', schema_version=1,
        measured_at_utc=datetime.now(timezone.utc).isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        tool_sha256=digest(__file__), audit=dict(path=str(audit_path.resolve()),sha256=digest(audit_path)),
        partition='validation', status='incomplete', records=[],
        limits=['Copy integrity and inventory only, not RAW pair equivalence or neural validation.',
                'All ISO variants of this composition stay outside training.',
                'No originals removed, renamed or modified; failed partial copies are not deleted.'])
    try:
        for record in records:
            source = Path(record['path'])
            result['records'].append(copy_verified(source, destination/source.name,
                                                   record['bytes'], record['mtime_ns']))
            print(f'COPY {len(result["records"])}/{len(records)} {source.name}', flush=True)
        result['status'] = 'copied_and_hash_verified'
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        with evidence.open('x',encoding='utf-8') as handle:
            json.dump(result,handle,indent=2,allow_nan=False)
            handle.write('\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('audit','destination','evidence'):
        parser.add_argument('--'+name,type=Path,required=True)
    args = parser.parse_args()
    run(args.audit,args.destination,args.evidence)
