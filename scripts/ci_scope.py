"""Conservative GitHub test plan, sharing the local test-selection rules."""
import argparse
import json
import os
from pathlib import Path
import subprocess

from build_windows import ROOT, RULES_PATH, matching, select_tests

DOC_PATTERNS = ['docs/**', 'README*', 'AGENTS.md', 'licenses/**', 'LICENSE*']
STANDARD_OS = ['windows-2022', 'ubuntu-24.04', 'macos-15']


def plan_scope(paths, event, branch, rules, base_available=True):
    # A branch name must never hide code changes, including patch_documentation.
    code = [p for p in paths if not matching(p, DOC_PATTERNS)]
    force = event in ('pull_request', 'workflow_dispatch') or branch in ('dev', 'main')
    plan = select_tests(code, rules, force_full=force or not base_available)
    native = plan['full'] or bool(plan['tests'])
    plan.update(schema=1, event=event, branch=branch, native=native,
                documents=any(matching(p, DOC_PATTERNS) for p in paths),
                paths=sorted(set(paths)), base_available=base_available,
                matrix={'os': STANDARD_OS + (['macos-15-intel'] if plan['full'] else [])})
    return plan


def changed_paths(base, head, root=ROOT):
    data = subprocess.check_output(['git', 'diff', '--name-only', '--no-renames', '-z',
                                    base, head, '--'], cwd=root)
    return [p.decode('utf8') for p in data.split(b'\0') if p]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'ci-logs/test-plan.json')
    args = parser.parse_args(argv)
    event = os.environ.get('GITHUB_EVENT_NAME', 'workflow_dispatch')
    branch = os.environ.get('GITHUB_REF_NAME', '')
    available = True
    try:
        if event == 'pull_request':
            payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf8'))
            base = payload['pull_request']['base']['sha']
        else:
            # Include earlier canceled/untested pushes on this same branch.
            base = subprocess.check_output(['git', 'merge-base', 'origin/dev', 'HEAD'],
                                           cwd=ROOT, text=True).strip()
        paths = changed_paths(base, 'HEAD')
    except (OSError, KeyError, ValueError, subprocess.CalledProcessError):
        available, paths = False, []  # Missing history fails closed to full validation.
    plan = plan_scope(paths, event, branch,
                      json.loads(RULES_PATH.read_text(encoding='utf8')), available)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(plan, indent=2), encoding='utf8')
    print(json.dumps(plan, indent=2))
    if os.environ.get('GITHUB_OUTPUT'):
        with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as output:
            for key, value in {'native': str(plan['native']).lower(),
                               'python': str(plan['full'] or bool(plan['python'])).lower(),
                               'documents': str(plan['documents']).lower(),
                               'matrix': json.dumps(plan['matrix'])}.items():
                output.write(f'{key}={value}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
