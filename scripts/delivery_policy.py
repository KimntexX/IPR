"""Authorize downloadable packages only for the exact CI-tested dev/main commit."""
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BRANCHES = {'dev', 'main'}
PLATFORMS = {'windows-2022', 'ubuntu-24.04', 'macos-15', 'macos-15-intel'}


def api(path):
    request = Request('https://api.github.com/' + path, headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'IPR-delivery-policy',
        'Authorization': 'Bearer ' + os.environ['GH_TOKEN']})
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def context(event, branch, sha, payload, repository):
    run_id = None
    if event == 'workflow_run':
        run = payload['workflow_run']
        branch, sha, run_id = run['head_branch'], run['head_sha'], run['id']
        if run['head_repository']['full_name'] != repository:
            raise ValueError('Foreign repository cannot deliver packages')
    elif event != 'workflow_dispatch':
        raise ValueError('Delivery requires a successful CI completion or manual branch dispatch')
    if branch not in BRANCHES or not re.fullmatch('[a-f0-9]{40}', sha):
        raise ValueError('Packages are reserved for dev/main and an exact commit SHA')
    return branch, sha, run_id


def validate_ci(run, jobs, branch, sha, repository):
    if (run['conclusion'] != 'success' or run['status'] != 'completed' or
        run['head_sha'] != sha or run['head_branch'] != branch or
        run['event'] not in ('push', 'workflow_dispatch') or
        run['path'].split('@')[0] != '.github/workflows/ci.yml' or
        run['head_repository']['full_name'] != repository):
        raise ValueError('No successful native CI for this exact dev/main commit')
    successful = {job['name'] for job in jobs if job['conclusion'] == 'success'}
    if not {f'native ({os_name})' for os_name in PLATFORMS}.issubset(successful):
        raise ValueError('Full four-platform native validation is required')
    if any(job['name'] == 'CI validation' for job in jobs) and 'CI validation' not in successful:
        raise ValueError('Aggregate CI validation did not pass')


def verify_publication_environment():
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf8'))
    repository = os.environ['GITHUB_REPOSITORY']
    branch, sha, run_id = context(os.environ['GITHUB_EVENT_NAME'],
                                 os.environ['GITHUB_REF_NAME'], os.environ['GITHUB_SHA'],
                                 payload, repository)
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if head != sha:
        raise ValueError('Package checkout differs from the tested commit')
    if subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], text=True).strip():
        raise ValueError('Cannot deliver modified tracked sources')
    if run_id is not None:
        runs = [api(f'repos/{repository}/actions/runs/{run_id}')]
    else:
        query = urlencode({'branch': branch, 'head_sha': sha, 'status': 'completed', 'per_page': 100})
        runs = api(f'repos/{repository}/actions/workflows/ci.yml/runs?{query}')['workflow_runs']
    for run in runs:
        if run.get('conclusion') != 'success':
            continue
        jobs = api(f'repos/{repository}/actions/runs/{run["id"]}/jobs?per_page=100')['jobs']
        try:
            validate_ci(run, jobs, branch, sha, repository)
        except ValueError:
            continue
        return {'branch': branch, 'sha': sha, 'ci_run_id': run['id']}
    raise ValueError('No completed, successful full CI exists for this dev/main commit')


def main():
    from ci import release_version
    approval = verify_publication_environment()
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf8'))
    version = payload.get('inputs', {}).get('version') or ''
    if not version:
        try:
            version = subprocess.check_output(['git', 'describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*'], text=True).strip()
        except subprocess.CalledProcessError:
            version = '0.1.0-alpha'
    version = release_version(version)[0].split('+')[0]
    approval['label'] = f'{version}+{approval["branch"]}.{approval["sha"][:12]}'
    print(json.dumps(approval))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as output:
            for key, value in approval.items():
                output.write(f'{key}={value}\n')


if __name__ == '__main__':
    main()
