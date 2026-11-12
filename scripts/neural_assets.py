"""Acquire pinned neural SDKs or a validated model, never during CMake configure."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def validate_source(item):
    if not isinstance(item.get('url'), str) or not item['url'].startswith('https://'):
        raise ValueError('A validated HTTPS asset URL is required')
    if not isinstance(item.get('sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', item['sha256']):
        raise ValueError('A validated SHA-256 is required; model export is not ready')


def fetch(item, destination):
    validate_source(item)
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file():
        with destination.open('rb') as existing:
            if hashlib.file_digest(existing, 'sha256').hexdigest() == item['sha256']:
                return
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            digest = hashlib.sha256()
            with urllib.request.urlopen(item['url'], timeout=60) as response:
                if not response.url.startswith('https://'):
                    raise ValueError('Asset download redirected away from HTTPS')
                while block := response.read(1024 * 1024):
                    digest.update(block)
                    output.write(block)
            if digest.hexdigest() != item['sha256']:
                raise ValueError(f'Asset SHA-256 mismatch: {item.get("id", "model")}')
        temporary.replace(destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def unpack(archive, destination):
    destination = Path(destination).resolve()
    with zipfile.ZipFile(archive) as package:
        for entry in package.infolist():
            path = PurePosixPath(entry.filename.replace('\\', '/'))
            target = destination.joinpath(*path.parts).resolve()
            if not target.is_relative_to(destination) or path.is_absolute() or ':' in entry.filename:
                raise ValueError('Unsafe archive member')
        package.extractall(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['runtime', 'model'])
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--manifest', type=Path)
    args = parser.parse_args()
    manifest = args.manifest or ROOT / 'packaging' / f'neural-{args.kind}.json'
    data = json.loads(manifest.read_text(encoding='utf-8'))
    if data.get('schema_version') != 1:
        raise ValueError('Unsupported neural asset manifest schema')
    if args.kind == 'model':
        fetch(data, args.output)
    else:
        for item in data['packages']:
            directory = item['directory']
            if not re.fullmatch(r'[a-zA-Z0-9._-]+', directory):
                raise ValueError('Unsafe package directory')
            archive = args.output / f'{directory}.nupkg'
            fetch(item, archive)
            unpack(archive, args.output / directory)


if __name__ == '__main__':
    main()
