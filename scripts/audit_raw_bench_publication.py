"""Read-only publication audit of unpushed Git blobs and new bench evidence.

No secret values are printed or recorded. This is a scoped heuristic check,
not a comprehensive secret scanner or a license/photographic validation.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MAX_BLOB = 100*1024**2  # Conservative local publication gate, not a service API.
PRIVATE_SUFFIXES = {'.cr2', '.cr3', '.dng', '.nef', '.arw', '.tif', '.tiff',
                    '.dcp', '.lcp', '.icc', '.icm', '.bin', '.exe', '.dll', '.lib',
                    '.zip', '.7z', '.pfx', '.p12', '.pem', '.key', '.sqlite', '.db'}
SECRETS = {
    'private_key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    'github_token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b'),
    'openai_key': re.compile(rb'\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}\b'),
    'aws_access_key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
}
SENSITIVE_KEYS = re.compile(r'^(?:gps.*|.*serialnumber|camera_serial|lens_serial|password|api_key|access_token|refresh_token|private_key|client_secret|profile_base64|icc_base64|dcp_base64|lcp_base64|image_base64|pixels_base64|raw_base64|pixel_data|raw_bytes|huesatmap|looktable|colormatrix[12]|tonecurve)$', re.I)


def git(root, *args, binary=False):
    return subprocess.check_output(['git', '-C', str(root), *args], text=not binary)


def inspect_content(data, path):
    findings = []
    suffix = Path(path).suffix.lower()
    if suffix in PRIVATE_SUFFIXES:
        findings.append(dict(kind='private_or_generated_file_type', suffix=suffix))
    if len(data) >= MAX_BLOB:
        findings.append(dict(kind='oversized_blob', bytes=len(data)))
    for name, pattern in SECRETS.items():
        matches = list(pattern.finditer(data))
        if matches:
            findings.append(dict(kind='credential_pattern', detector=name, occurrences=len(matches)))
    if suffix == '.json':
        def reject(value):
            raise ValueError('Nonfinite number: '+value)
        try:
            value = json.loads(data.decode('utf-8-sig'), parse_constant=reject)
        except (ValueError, UnicodeError) as error:
            findings.append(dict(kind='invalid_json', error_type=type(error).__name__))
            return findings
        def walk(item, key_path='$'):
            if isinstance(item, dict):
                for key, child in item.items():
                    location = key_path+'.'+key
                    nonempty = child is not None and child != '' and child != [] and child != {}
                    if SENSITIVE_KEYS.fullmatch(key) and nonempty:
                        findings.append(dict(kind='sensitive_json_field', key_path=location,
                                             value_type=type(child).__name__))
                    walk(child, location)
            elif isinstance(item, list):
                for i, child in enumerate(item):
                    if isinstance(child, (dict, list)):
                        walk(child, key_path+f'[{i}]')
        walk(value)
    return findings


def unpublished_blobs(root, revision):
    lines = git(root, 'rev-list', '--objects', revision, '--not', '--remotes=origin').splitlines()
    paths = {line.split(' ', 1)[0]:line.split(' ', 1)[1] if ' ' in line else '' for line in lines}
    if not paths:
        return []
    metadata = subprocess.check_output(['git', '-C', str(root), 'cat-file',
        '--batch-check=%(objectname) %(objecttype) %(objectsize)'],
        input='\n'.join(paths)+'\n', text=True)
    blobs = []
    for row in metadata.splitlines():
        oid, kind, size = row.split()
        if kind == 'blob':
            blobs.append(dict(oid=oid, path=paths[oid], bytes=int(size)))
    return blobs


def audit(root, ref, output):
    revision = git(root, 'rev-parse', ref).strip()
    blobs = unpublished_blobs(root, revision)
    records = []
    # One cat-file process; at most one blob payload is retained at a time.
    with subprocess.Popen(['git', '-C', str(root), 'cat-file', '--batch'],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE) as reader:
        try:
            for blob in blobs:
                reader.stdin.write((blob['oid']+'\n').encode('ascii')); reader.stdin.flush()
                header = reader.stdout.readline().decode('ascii').split()
                if len(header) != 3 or header[0] != blob['oid'] or int(header[2]) != blob['bytes']:
                    raise ValueError('Unexpected Git blob layout')
                data = reader.stdout.read(blob['bytes'])
                if len(data) != blob['bytes'] or reader.stdout.read(1) != b'\n':
                    raise ValueError('Truncated Git blob')
                records.append(dict(**blob, sha256=hashlib.sha256(data).hexdigest(),
                                    findings=inspect_content(data, blob['path'])))
        finally:
            reader.stdin.close()
            try:
                reader.wait(timeout=10)
            except subprocess.TimeoutExpired:
                reader.kill(); reader.wait(timeout=10)
            reader.stdout.close()
    untracked = []
    for path in git(root, 'ls-files', '--others', '--exclude-standard').splitlines():
        file = root/path
        if file.resolve() == output.resolve():
            continue
        data = file.read_bytes()
        untracked.append(dict(path=path, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                              findings=inspect_content(data, path)))
    tracked = git(root, 'ls-tree', '-r', '--name-only', revision).splitlines()
    private_tip = [p for p in tracked if Path(p).suffix.lower() in PRIVATE_SUFFIXES]
    # Inherited public files are inventoried, not silently counted as new payload.
    counts = Counter(f['kind'] for r in records+untracked for f in r['findings'])
    remotes = git(root, 'for-each-ref', '--format=%(refname) %(objectname)', 'refs/remotes/origin').splitlines()
    return dict(schema_version=1, created_utc=datetime.now(timezone.utc).isoformat(),
        ref=ref, revision=revision, remote_refs_at_audit=remotes,
        unpublished_blob_count=len(records), unpublished_bytes=sum(r['bytes'] for r in records),
        largest_unpublished=sorted(records, key=lambda r:r['bytes'], reverse=True)[:12],
        records=records, untracked=untracked, inherited_private_type_paths=private_tip,
        finding_counts=dict(counts), verdict='blocked_for_review' if counts else 'pass_scoped_publication_checks',
        scope=['All blobs reachable from the branch but not from any fetched origin reference, including history.',
               'All nonignored untracked files, except this audit output.',
               'Only selected credential patterns and named sensitive JSON fields; not an exhaustive security scan.',
               'Inherited files already reachable on origin are inventoried separately, not rescanned.',
               'Local Windows paths, source names, camera models, settings and hashes remain as evidence provenance.',
               'No guarantee of external private corpus availability or photographic quality; hashes are not backups.',
               'No legal license clearance inferred from the absence of embedded profile binaries.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--ref', default='HEAD')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    out = args.output if args.output.is_absolute() else root/args.output
    result = audit(root, args.ref, out)
    with out.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, allow_nan=False, indent=2)
    print(json.dumps({k:result[k] for k in ('verdict','unpublished_blob_count','unpublished_bytes','finding_counts')}))
    return 0 if result['verdict'].startswith('pass_') else 1


if __name__ == '__main__':
    raise SystemExit(main())
