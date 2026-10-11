"""Reject release tags not pointing at a validated dev merge; never create tags."""
import subprocess
import sys
from ci import release_version


def verify(tag):
    release_version(tag)
    if not tag.startswith('v'): raise ValueError('Release tags must start with v')
    commit = subprocess.check_output(['git', 'rev-parse', tag + '^{commit}'], text=True).strip()
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    if commit != head: raise ValueError('Checkout differs from release tag')
    subprocess.run(['git', 'merge-base', '--is-ancestor', commit, 'origin/dev'], check=True)
    parents = subprocess.check_output(['git', 'rev-list', '--parents', '-n', '1', commit], text=True).split()
    if len(parents) < 3: raise ValueError('Expected a validated --no-ff dev merge')
    print('Release ancestry verified:', tag, commit)


if __name__ == '__main__':
    verify(sys.argv[1])
