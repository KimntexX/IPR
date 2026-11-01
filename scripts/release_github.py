"""Plan, inventory and publish verified production artifacts without rebuilding."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import tarfile
from urllib.error import HTTPError
from urllib.parse import quote
import zipfile

import delivery_policy as policy
from ci import cache, release_version

ROOT = Path(__file__).resolve().parents[1]
HOSTS = {'windows-2022': 'windows-x64', 'ubuntu-24.04': 'ubuntu-24.04-x64',
         'macos-15': 'darwin-arm64', 'macos-15-intel': 'darwin-x64'}
APPROVAL_KEYS = ('branch', 'sha', 'main_sha', 'build_sha', 'dev_ci_run_id',
                 'ci_run_id', 'closure_sha256', 'allowlist_sha256')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2, allow_nan=False)
        stream.write('\n')


def version_from_tag(tag):
    """Parse a main release label without changing the GitHub tag identity."""
    if not re.fullmatch(r'(?:release-)?v[0-9][A-Za-z0-9.+-]*', tag):
        raise ValueError('An existing version tag beginning with v or release-v is required')
    return release_version(tag.removeprefix('release-'))[0]


def existing_tag_commit(repository, tag):
    version_from_tag(tag)
    obj = policy.api(f'repos/{repository}/git/ref/tags/{quote(tag, safe="")}')['object']
    for _ in range(8):
        if not policy.SHA.fullmatch(obj.get('sha', '')):
            break
        if obj['type'] == 'commit':
            return obj['sha']
        if obj['type'] != 'tag':
            break
        obj = policy.api(f'repos/{repository}/git/tags/{obj["sha"]}')['object']
    raise ValueError('Tag does not resolve to a commit')


def validate_plan(plan):
    if plan.get('schema') != 1:
        raise ValueError('Unsupported release plan')
    if plan.get('published_configurations') != json.loads(policy.matrix_value(policy.PACKAGE_MATRIX))['include']:
        raise ValueError('Release plan differs from the selected publication targets')
    repository = os.environ['GITHUB_REPOSITORY']
    if plan.get('repository') != repository:
        raise ValueError('Foreign release plan')
    approval = policy.authorize_main(plan['main_sha'], repository, plan['ci_run_id'])
    if any(plan.get(key) != approval.get(key) for key in APPROVAL_KEYS):
        raise ValueError('Release plan differs from its live two-gate attestation')
    if existing_tag_commit(repository, plan['tag']) != plan['main_sha']:
        raise ValueError('Release tag must already point to the exact promoted main commit')
    if version_from_tag(plan['tag']) != plan['version']:
        raise ValueError('Release tag and version disagree')
    if not policy.git('show', f'{plan["build_sha"]}:LICENSE'):
        raise ValueError('A project LICENSE must be present on the tested source')
    return approval


def make_plan(tag):
    if os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch' or os.environ.get('GITHUB_REF') != 'refs/heads/main':
        raise ValueError('GitHub releases require an explicit dispatch on main')
    approval = policy.verify_publication_environment(planning=True)
    if approval['branch'] != 'main' or policy.git('rev-parse', 'HEAD') != approval['main_sha']:
        raise ValueError('Release planning must check out its exact main commit')
    plan = dict(schema=1, repository=os.environ['GITHUB_REPOSITORY'], tag=tag,
                version=version_from_tag(tag), created_utc=datetime.now(timezone.utc).isoformat(),
                published_configurations=json.loads(policy.matrix_value(policy.PACKAGE_MATRIX))['include'], **approval)
    validate_plan(plan)
    return plan


def archive_manifest(path):
    """Read metadata, never extract an archive into the runner filesystem."""
    def select(names):
        for name in names:
            parsed = PurePosixPath(name)
            if parsed.is_absolute() or '..' in parsed.parts or '\\' in name:
                raise ValueError('Unsafe package member name')
        found = [name for name in names if len(PurePosixPath(name).parts) == 2
                 and PurePosixPath(name).name == 'package-manifest.json']
        if len(found) != 1:
            raise ValueError('Package must contain exactly one root manifest')
        return found[0]
    if path.suffix == '.zip':
        with zipfile.ZipFile(path) as stream:
            name = select(stream.namelist())
            if stream.getinfo(name).file_size > 16 * 1024 * 1024:
                raise ValueError('Package manifest exceeds 16 MiB')
            return json.loads(stream.read(name))
    with tarfile.open(path, 'r:gz') as stream:
        member = stream.getmember(select(stream.getnames()))
        if not member.isfile() or member.size > 16 * 1024 * 1024:
            raise ValueError('Invalid package manifest member')
        with stream.extractfile(member) as data:
            return json.load(data)


def check_package(manifest, plan, runner, mode):
    if (manifest.get('commit') != plan['build_sha'] or manifest.get('source_dirty') is not False or
        manifest.get('version') != plan['version'] or manifest.get('mode') != mode or
        manifest.get('platform') != HOSTS[runner] or manifest.get('private_profiles') is not False or
        manifest.get('smoke', {}).get('startup') != 'passed'):
        raise ValueError('Package identity, source, platform or smoke evidence mismatch')
    approval = manifest.get('github_delivery_approval') or {}
    if any(approval.get(key) != plan.get(key) for key in APPROVAL_KEYS):
        raise ValueError('Package lacks the matching two-gate approval')


def inventory(plan, plan_path, directory, build, runner, mode):
    validate_plan(plan)
    if (runner, mode) not in policy.PACKAGE_MATRIX:
        raise ValueError('Unsupported release matrix entry')
    state = cache(build)
    expected = {'BUILD_TESTING': 'OFF', 'IPR_BUILD_TOOLS': 'OFF',
                'IPR_DEPLOY_MODE': mode, 'IPR_RELEASE_LABEL': plan['version']}
    if any(state.get(key) != value for key, value in expected.items()):
        raise ValueError('Release build is not the attested production configuration')
    if policy.git('rev-parse', 'HEAD') != plan['build_sha'] or policy.git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Release artifacts must be built from clean pinned dev sources')
    payloads = sorted(p for p in directory.iterdir() if p.is_file() and not p.name.endswith('.sha256'))
    archives = [p for p in payloads if p.name.endswith(('.zip', '.tar.gz'))]
    installers = [p for p in payloads if p.name.endswith('-setup.exe')]
    expected_installers = 1 if (runner, mode) == ('windows-2022', 'standalone') else 0
    if len(archives) != 1 or len(installers) != expected_installers or len(payloads) != len(archives) + len(installers):
        raise ValueError('Unexpected or missing release payload')
    package = archive_manifest(archives[0])
    check_package(package, plan, runner, mode)
    files = []
    for path in payloads:
        sidecar = path.with_name(path.name + '.sha256')
        expected_checksum = digest(path) + '  ' + path.name
        if sidecar.read_text(encoding='ascii').strip() != expected_checksum:
            raise ValueError('Package checksum mismatch: ' + path.name)
        for item in (path, sidecar):
            files.append({'name': item.name, 'bytes': item.stat().st_size, 'sha256': digest(item)})
    return dict(schema=1, runner=runner, mode=mode, tag=plan['tag'],
                plan_sha256=digest(plan_path), approval={key: plan[key] for key in APPROVAL_KEYS},
                production_options=expected, smoke=package['smoke'],
                build_dependencies=package.get('build_dependencies', {}), files=files)


def collect(plan, plan_path, directory):
    """Verify every downloaded payload against its build-job inventory."""
    records = []
    entries, paths = set(), set()
    inventories = sorted(directory.glob('release-inventory-*.json'))
    for path in inventories:
        record = json.loads(path.read_text(encoding='utf-8'))
        entry = (record['runner'], record['mode'])
        if entry not in policy.PACKAGE_MATRIX or entry in entries:
            raise ValueError('Unexpected or duplicate release matrix inventory')
        entries.add(entry)
        if (record.get('schema') != 1 or record.get('tag') != plan['tag'] or
            record.get('plan_sha256') != digest(plan_path) or
            record.get('approval') != {key: plan[key] for key in APPROVAL_KEYS} or
            record.get('production_options') != {'BUILD_TESTING': 'OFF', 'IPR_BUILD_TOOLS': 'OFF',
                'IPR_DEPLOY_MODE': entry[1], 'IPR_RELEASE_LABEL': plan['version']} or
            record.get('smoke', {}).get('startup') != 'passed' or not record.get('files')):
            raise ValueError('Downloaded inventory lacks matching production provenance')
        for item in record['files']:
            name = item['name']
            if (not name or name in ('.', '..') or ':' in name or
                PurePosixPath(name).name != name or '\\' in name or name.casefold() in paths):
                raise ValueError('Unsafe or duplicate release filename')
            paths.add(name.casefold())
            file = directory / name
            if file.is_symlink() or file.stat().st_size != item['bytes'] or digest(file) != item['sha256']:
                raise ValueError('Downloaded artifact differs from the verified payload: ' + name)
        records.append(record)
    if entries != set(policy.PACKAGE_MATRIX):
        raise ValueError('Incomplete release matrix')
    allowed = paths | {p.name.casefold() for p in inventories}
    if any(p.is_dir() or p.name.casefold() not in allowed for p in directory.iterdir()):
        raise ValueError('Unrecorded downloaded artifact')
    return records


def publish(plan, plan_path, directory):
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    if (os.environ.get('GITHUB_EVENT_NAME') != 'workflow_dispatch' or os.environ.get('GITHUB_REF') != 'refs/heads/main' or
        str(event.get('inputs', {}).get('publish', '')).lower() != 'true' or
        event.get('inputs', {}).get('tag') != plan['tag'] or os.environ.get('GITHUB_SHA') != plan['main_sha']):
        raise ValueError('Publishing requires an explicit main dispatch with publish=true and the same tag')
    validate_plan(plan)
    records = collect(plan, plan_path, directory)
    if policy.git('rev-parse', 'HEAD') != plan['main_sha'] or policy.git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Publishing must check out the clean attested main commit')
    repository = plan['repository']
    try:
        policy.api(f'repos/{repository}/releases/tags/{quote(plan["tag"], safe="")}')
    except HTTPError as error:
        try:
            if error.code != 404:
                raise
        finally:
            error.close()
    else:
        raise ValueError('Release already exists; immutable payloads are never overwritten')
    provenance = directory / ('IPR-' + plan['tag'] + '-provenance.json')
    write_json(provenance, dict(schema=1, plan=plan, artifacts=records,
                               published_payloads='The same bytes verified by the build and download jobs; no rebuild'))
    checksum = provenance.with_name(provenance.name + '.sha256')
    with checksum.open('x', encoding='ascii') as stream:
        stream.write(digest(provenance) + '  ' + provenance.name + '\n')
    assets = [directory / item['name'] for record in records for item in record['files']] + [provenance, checksum]
    notes = directory.parent / 'release-notes.md'
    with notes.open('x', encoding='utf-8') as stream:
        stream.write(f'Production sources: `{plan["build_sha"]}` (tested dev).\n\n'
                     f'Promoted main: `{plan["main_sha"]}`. Closure: `{plan["closure_sha256"]}`.\n\n'
                     'Archives include no private camera profiles. See the validation summary and attached provenance.\n')
    command = ['gh', 'release', 'create', plan['tag'], *map(str, assets), '--repo', repository,
               '--verify-tag', '--draft', '--title', 'IPR ' + plan['tag'], '--notes-file', str(notes)]
    if '-alpha' in plan['version'] or '-beta' in plan['version']:
        command.append('--prerelease')
    subprocess.run(command, cwd=ROOT, check=True, timeout=180)
    # Leave an interrupted upload as a draft; never move tags or replace assets.
    validate_plan(plan)
    subprocess.run(['gh', 'release', 'edit', plan['tag'], '--repo', repository, '--draft=false'],
                   cwd=ROOT, check=True, timeout=60)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', required=True, choices=('plan', 'inventory', 'verify', 'publish'))
    parser.add_argument('--tag')
    parser.add_argument('--plan', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--directory', type=Path, default=Path('dist'))
    parser.add_argument('--build', type=Path, default=Path('build-ci'))
    parser.add_argument('--runner', choices=sorted(HOSTS))
    parser.add_argument('--mode', choices=('system', 'standalone'))
    args = parser.parse_args(argv)
    if args.action == 'plan':
        if not args.tag or not args.output:
            parser.error('plan requires --tag and --output')
        plan = make_plan(args.tag)
        write_json(args.output, plan)
        policy.emit(dict({key: plan[key] for key in ('build_sha', 'main_sha', 'tag', 'version')},
                         package_matrix=policy.matrix_value(policy.PACKAGE_MATRIX)))
        return 0
    if not args.plan:
        parser.error('--plan is required')
    plan = json.loads(args.plan.read_text(encoding='utf-8'))
    if args.action == 'inventory':
        if not args.output or not args.runner or not args.mode:
            parser.error('inventory requires --output, --runner and --mode')
        write_json(args.output, inventory(plan, args.plan, args.directory, args.build, args.runner, args.mode))
    elif args.action == 'verify':
        validate_plan(plan)
        collect(plan, args.plan, args.directory)
    else:
        publish(plan, args.plan, args.directory)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
