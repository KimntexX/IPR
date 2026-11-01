import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import promote_main as promote  # noqa: E402

ALLOWLIST = """
[main]
src/
docs/user/
README.md
packaging/main-allowlist.txt

[dev]
tests/
docs/validation/
src/tools/
"""


class Patterns(unittest.TestCase):
    def test_segments(self):
        self.assertTrue(promote.pattern_regex('scripts/fit_*.py').match('scripts/fit_tone.py'))
        self.assertFalse(promote.pattern_regex('scripts/fit_*.py').match('scripts/sub/fit_tone.py'))
        self.assertTrue(promote.pattern_regex('docs/').match('docs/a/b.md'))
        self.assertFalse(promote.pattern_regex('docs/').match('docs-other/a.md'))
        self.assertTrue(promote.pattern_regex('src/**/x.h').match('src/a/b/x.h'))

    def test_sections_and_errors(self):
        sections = promote.parse_allowlist(ALLOWLIST)
        self.assertIn('tests/', sections['dev'])
        with self.assertRaises(ValueError):
            promote.parse_allowlist('orphan/\n')
        with self.assertRaises(ValueError):
            promote.parse_allowlist('[other]\nx\n')

    def test_classification(self):
        sections = promote.parse_allowlist(ALLOWLIST)
        groups = promote.classify(['src/a.cpp', 'src/tools/t.cpp', 'tests/t.py', 'new.txt'], sections)
        self.assertEqual(groups['dev'], ['tests/t.py'])
        self.assertEqual(groups['unclassified'], ['new.txt'])
        # src/ and src/tools/ both match: a path must belong to exactly one side.
        self.assertEqual(groups['ambiguous'], ['src/tools/t.cpp'])
        self.assertEqual(groups['main'], ['src/a.cpp'])


class Links(unittest.TestCase):
    def test_dev_links_point_at_the_commit(self):
        dev = {'docs/validation/raw/a.json', 'docs/validation/matrix.md'}
        text = ('[a](../validation/raw/a.json) [m](../validation/matrix.md#row) [f](../validation/raw/) '
                '[kept](guide.md) [web](https://example.org/x) [anchor](#top)')
        out = promote.rewrite_document(text, 'docs/user/index.md', dev, {'docs', 'docs/user'}, 'abc')
        base = promote.REPOSITORY
        self.assertIn(f'[a]({base}/blob/abc/docs/validation/raw/a.json)', out)
        self.assertIn(f'[m]({base}/blob/abc/docs/validation/matrix.md#row)', out)
        self.assertIn(f'[f]({base}/tree/abc/docs/validation/raw)', out)
        self.assertIn('[kept](guide.md)', out)
        self.assertIn('[web](https://example.org/x)', out)
        self.assertIn('[anchor](#top)', out)


def git(directory, *args, **kwargs):
    return subprocess.run(['git', *args], cwd=directory, check=True, capture_output=True, text=True,
                          **kwargs).stdout.strip()


class Promotion(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        git(self.root, 'init', '-q', '-b', 'main')
        git(self.root, 'config', 'user.email', 'test@example.org')
        git(self.root, 'config', 'user.name', 'test')
        git(self.root, 'config', 'core.autocrlf', 'false')
        self.write('README.md', 'old main\n')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'main')
        self.main = git(self.root, 'rev-parse', 'HEAD')
        git(self.root, 'checkout', '-q', '-b', 'dev')
        self.write('packaging/main-allowlist.txt', ALLOWLIST.replace(
            '[main]\nsrc/\n', '[main]\nsrc/core/\n'))
        self.write('src/core/a.cpp', 'int a;\n')
        self.write('src/tools/t.cpp', 'int t;\n')
        self.write('tests/test_a.py', 'pass\n')
        self.write('docs/validation/raw/m.json', '{}\n')
        self.write('docs/user/guide.md', 'See [data](../validation/raw/m.json).\n')
        self.write('README.md', 'IPR\n')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'dev')
        self.dev = git(self.root, 'rev-parse', 'HEAD')
        self.previous = promote.ROOT
        promote.ROOT = self.root

    def tearDown(self):
        promote.ROOT = self.previous
        self.directory.cleanup()

    def write(self, path, text):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(text.encode('utf-8'))

    def test_snapshot_without_dev_ancestry(self):
        self.assertEqual(promote.main(['--source', self.dev, '--parent', self.main, '--write', '--ci-run-id', '42']), 0)
        promoted = git(self.root, 'rev-parse', 'main_promotion')
        self.assertEqual(git(self.root, 'log', '--format=%P', '-1', promoted), self.main)
        files = set(git(self.root, 'ls-tree', '-r', '--name-only', promoted).splitlines())
        self.assertEqual(files, {'README.md', 'packaging/main-allowlist.txt', 'src/core/a.cpp',
                                 'docs/user/guide.md', 'release-manifest.json'})
        guide = git(self.root, 'show', f'{promoted}:docs/user/guide.md')
        self.assertIn(f'{promote.REPOSITORY}/blob/{self.dev}/docs/validation/raw/m.json', guide)
        manifest = json.loads(git(self.root, 'show', f'{promoted}:release-manifest.json'))
        self.assertEqual(manifest['source']['commit'], self.dev)
        self.assertEqual(manifest['excluded_files'], 3)
        records = {r['path']: r for r in manifest['files']}
        source = (self.root / 'src/core/a.cpp').read_bytes()
        self.assertEqual(records['src/core/a.cpp']['sha256'], hashlib.sha256(source).hexdigest())
        self.assertEqual(records['docs/user/guide.md']['derived'], 'links_to_dev')
        self.assertEqual(manifest['source']['ci'], {'run_id': '42', 'workflow': 'CI native full tests'})
        self.assertEqual(manifest['production_profile']['IPR_BUILD_TOOLS'], 'OFF')
        self.assertEqual(manifest['closure_sha256'], promote.closure(manifest['files']))
        self.assertEqual(records['docs/user/guide.md']['sha256'],
                         hashlib.sha256((guide + '\n').encode('utf-8')).hexdigest())
        # The working tree and the dev branch are untouched.
        self.assertEqual(git(self.root, 'rev-parse', 'HEAD'), self.dev)
        self.assertEqual(git(self.root, 'status', '--porcelain'), '')

    def test_unknown_file_blocks(self):
        self.write('notes.txt', 'x\n')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'unclassified')
        source = git(self.root, 'rev-parse', 'HEAD')
        self.assertEqual(promote.main(['--source', source, '--parent', self.main, '--write', '--ci-run-id', '42']), 1)
        self.assertNotIn('main_promotion', git(self.root, 'branch', '--list', 'main_promotion'))

    def test_write_needs_the_ci_run(self):
        with self.assertRaises(SystemExit):
            promote.main(['--source', self.dev, '--parent', self.main, '--write'])

    def test_verify(self):
        promote.main(['--source', self.dev, '--parent', self.main, '--write', '--ci-run-id', '42'])
        promoted = git(self.root, 'rev-parse', 'main_promotion')
        self.assertEqual(promote.verify(promoted), [])
        # A file edited on main, or added there, breaks the correspondence.
        git(self.root, 'checkout', '-q', 'main_promotion')
        self.write('src/core/a.cpp', 'int b;\n')
        self.write('extra.txt', 'x\n')
        git(self.root, 'add', '-A')
        git(self.root, 'commit', '-q', '-m', 'edited on main')
        problems = promote.verify(git(self.root, 'rev-parse', 'HEAD'))
        self.assertIn('File differs from the manifest: src/core/a.cpp', problems)
        self.assertIn('File not in the manifest: extra.txt', problems)

    def test_dry_run_writes_nothing(self):
        self.assertEqual(promote.main(['--source', self.dev, '--parent', self.main]), 0)
        self.assertEqual(git(self.root, 'branch', '--list', 'main_promotion'), '')


if __name__ == '__main__':
    unittest.main()
