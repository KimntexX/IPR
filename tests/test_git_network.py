import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


SPEC = importlib.util.spec_from_file_location('git_network', Path(__file__).parents[1] / 'scripts/git_network.py')
network = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(network)


class NetworkTests(unittest.TestCase):
    def test_compression_retains_both_merge_parents(self):
        commits = {
            'a': {'parents': []}, 'b': {'parents': ['a']},
            'c': {'parents': ['b']}, 'd': {'parents': ['b']},
            'e': {'parents': ['c', 'd']},
        }
        refs = {'dev': 'e', 'patch_ui': 'd'}
        _, _, nodes, edges = network.layout(commits, refs, {'e': ['dev'], 'd': ['patch_ui']}, set())
        self.assertEqual(set(nodes), {'a', 'b', 'd', 'e'})
        self.assertEqual(set(edges), {('a', 'b'), ('b', 'd'), ('b', 'e'), ('d', 'e')})
        _, _, full, edges = network.layout(commits, refs, {}, set(), full=True)
        self.assertEqual(len(full), 5)
        self.assertIn(('c', 'e'), edges)

    def test_real_repository_is_escaped_and_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                return network.git(root, *args)
            git('init', '-b', 'dev')
            git('config', 'user.name', 'Graph Test')
            git('config', 'user.email', 'graph@example.invalid')
            git('commit', '--allow-empty', '-m', 'root <script>&')
            git('tag', '-a', 'v0.1.1-alpha', '-m', 'validated')
            git('branch', 'patch_ui')
            before = git('show-ref')
            html, svg = network.render(root)
            ET.fromstring(svg)
            self.assertIn('root &lt;script&gt;&amp;', svg)
            self.assertIn('v0.1.1-alpha', svg)
            self.assertIn('origin', html)
            self.assertEqual(before, git('show-ref'))
            self.assertEqual(git('status', '--porcelain'), '')
            network.main(['--repo', str(root)])
            self.assertTrue((root / 'build-git-network/network.html').is_file())

    def test_empty_repository_reports_error(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.check_call(['git', 'init', '-q', directory])
            with self.assertRaises(ValueError):
                network.read_history(Path(directory))


if __name__ == '__main__':
    unittest.main()
