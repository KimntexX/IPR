"""Authorize exact dev packages or promoted main packages with two CI gates."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
from urllib.parse import urlencode
from urllib.request import Request, urlopen

BRANCHES = {'dev', 'main'}
PLATFORMS = {'windows-2022', 'ubuntu-24.04', 'macos-15', 'macos-15-intel'}
PACKAGE_MATRIX = tuple((host, mode) for host in sorted(PLATFORMS)
                       for mode in ('system', 'standalone')
                       if (host, mode) != ('ubuntu-24.04', 'system'))
CI_WORKFLOWS = {'dev': '.github/workflows/ci.yml', 'main': '.github/workflows/main.yml'}
DEV_WORKFLOW_NAMES = {'CI scoped validation', 'CI native full tests', CI_WORKFLOWS['dev']}
SHA = re.compile(r'[a-f0-9]{40}\Z')
ROOT = Path(__file__).resolve().parents[1]


def api(path):
    request = Request('https://api.github.com/' + path, headers={
        'Accept': 'application/vnd.github+json', 'User-Agent': 'IPR-delivery-policy',
        'Authorization': 'Bearer ' + os.environ['GH_TOKEN']})
    with urlopen(request, timeout=30) as response:
        return json.load(response)


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def context(event, branch, sha, payload, repository):
    run_id = None
    if event == 'workflow_run':
        run = payload['workflow_run']
        branch, sha, run_id = run['head_branch'], run['head_sha'], run['id']
        if run['head_repository']['full_name'] != repository:
            raise ValueError('Foreign repository cannot deliver packages')
    elif event != 'workflow_dispatch':
        raise ValueError('Delivery requires a successful CI completion or manual branch dispatch')
    if branch not in BRANCHES or not SHA.fullmatch(sha):
        raise ValueError('Packages are reserved for dev/main and an exact commit SHA')
    return branch, sha, run_id


def validate_ci(run, jobs, branch, sha, repository):
    if (run.get('conclusion') != 'success' or run.get('status') != 'completed' or
        run.get('head_sha') != sha or run.get('head_branch') != branch or
        run.get('event') not in ('push', 'workflow_dispatch') or
        run.get('path', '').split('@')[0] != CI_WORKFLOWS[branch] or
        run.get('head_repository', {}).get('full_name') != repository):
        raise ValueError('No successful trusted CI for this exact branch commit')
    required = ({f'native ({host})' for host in PLATFORMS} | {'CI validation'} if branch == 'dev'
                else {f'production ({host}, {mode})' for host, mode in PACKAGE_MATRIX}
                | {'Promotion verification', 'Main production validation'})
    successful = {job['name'] for job in jobs if job.get('conclusion') == 'success'}
    if not required.issubset(successful) or any(
            job['name'] in required and job.get('conclusion') != 'success' for job in jobs):
        raise ValueError('Required full CI jobs are missing, skipped or unsuccessful: ' +
                         ', '.join(sorted(required - successful)))


def run_jobs(repository, run_id):
    jobs = []
    for page in range(1, 101):
        result = api(f'repos/{repository}/actions/runs/{run_id}/jobs?per_page=100&page={page}')
        batch = result['jobs']
        jobs.extend(batch)
        if len(batch) < 100:
            return jobs
    raise ValueError('CI job pagination limit exceeded')


def require_ci(branch, sha, repository, run_id=None):
    if not SHA.fullmatch(sha):
        raise ValueError('A full source SHA is required')
    if run_id is not None:
        if not str(run_id).isdigit() or int(run_id) <= 0:
            raise ValueError('Invalid CI run identifier')
        runs = [api(f'repos/{repository}/actions/runs/{int(run_id)}')]
    else:
        query = urlencode({'branch': branch, 'head_sha': sha, 'status': 'completed', 'per_page': 100})
        workflow = CI_WORKFLOWS[branch].rsplit('/', 1)[1]
        runs = api(f'repos/{repository}/actions/workflows/{workflow}/runs?{query}')['workflow_runs']
    for run in runs:
        if run.get('conclusion') != 'success':
            continue
        try:
            validate_ci(run, run_jobs(repository, run['id']), branch, sha, repository)
        except ValueError:
            continue
        return run['id']
    raise ValueError(f'No completed, successful {branch} CI exists for this exact commit')


def verify_promotion(main_sha, repository):
    """Read-only closure check plus the pinned dev CI; main CI cannot attest itself."""
    from promote_main import verify, MANIFEST, POLICY, PRODUCTION_PROFILE
    if not SHA.fullmatch(main_sha):
        raise ValueError('A full main SHA is required')
    manifest = json.loads(git('show', f'{main_sha}:{MANIFEST}'))
    source = manifest['source']
    ci = source['ci']
    source_sha = source['commit']
    if (manifest.get('schema') != 1 or manifest.get('policy') != POLICY or
        source.get('repository') != 'https://github.com/' + repository or
        not SHA.fullmatch(source_sha) or ci.get('workflow') not in DEV_WORKFLOW_NAMES or
        manifest.get('production_profile') != PRODUCTION_PROFILE or not ci.get('run_id')):
        raise ValueError('Unsupported or incomplete promotion attestation')
    problems = verify(main_sha)
    if problems:
        raise ValueError('Promotion differs from its tested source: ' + '; '.join(problems))
    dev_run = require_ci('dev', source_sha, repository, ci['run_id'])
    return {'branch': 'main', 'sha': main_sha, 'main_sha': main_sha,
            'build_sha': source_sha, 'dev_ci_run_id': dev_run,
            'closure_sha256': manifest['closure_sha256'],
            'allowlist_sha256': manifest['allowlist']['sha256']}


def authorize_main(main_sha, repository, run_id=None):
    approval = verify_promotion(main_sha, repository)
    approval['ci_run_id'] = require_ci('main', main_sha, repository, run_id)
    return approval


def verify_publication_environment(*, planning=False):
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf8'))
    repository = os.environ['GITHUB_REPOSITORY']
    branch, sha, run_id = context(os.environ['GITHUB_EVENT_NAME'],
                                 os.environ['GITHUB_REF_NAME'], os.environ['GITHUB_SHA'],
                                 payload, repository)
    head = git('rev-parse', 'HEAD')
    if git('status', '--porcelain', '--untracked-files=no'):
        raise ValueError('Cannot deliver modified tracked sources')
    if branch == 'dev':
        approval = {'branch': branch, 'sha': sha, 'build_sha': sha,
                    'ci_run_id': require_ci(branch, sha, repository, run_id)}
    else:
        approval = authorize_main(sha, repository, run_id)
    # Planning inspects main; packages MUST be built from its pinned dev SHA.
    expected_head = approval['sha'] if planning else approval['build_sha']
    if head != expected_head:
        raise ValueError('Package checkout differs from the attested commits')
    return approval


def emit(approval):
    print(json.dumps(approval))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as output:
            for key, value in approval.items():
                output.write(f'{key}={value}\n')


def main(argv=None):
    from ci import release_version
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-promotion', metavar='SHA', help='closure + dev gate only; for main CI itself')
    args = parser.parse_args(argv)
    if args.check_promotion:
        emit(verify_promotion(args.check_promotion, os.environ['GITHUB_REPOSITORY']))
        return 0
    approval = verify_publication_environment(planning=True)
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf8'))
    version = payload.get('inputs', {}).get('version') or ''
    if not version:
        try:
            version = git('describe', '--tags', '--abbrev=0', '--match', 'v[0-9]*', approval['build_sha'])
        except subprocess.CalledProcessError:
            version = '0.1.0-alpha'
    version = release_version(version)[0].split('+')[0]
    approval['label'] = f'{version}+{approval["branch"]}.{approval["sha"][:12]}'
    emit(approval)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
