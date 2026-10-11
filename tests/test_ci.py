import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ipr_ci', ROOT / 'scripts/ci.py')
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class DeploymentTests(unittest.TestCase):
    def test_version_phases(self):
        for label in ('v0.1.7-alpha', '0.2.1-beta', 'v0.3.2-beta', 'v1.0.0'):
            self.assertEqual(ci.release_version(label)[0], label.removeprefix('v'))
        for label in ('0.1.0', '0.1.0-beta', '0.2.0-alpha', '../../oops', '1.0.0-alpha'):
            with self.assertRaises(ValueError): ci.release_version(label)

    def test_private_payload_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            for filename in ('profile.DCP', 'lens.lcp', 'scene.cr3', 'raw-normalization.json'):
                file = stage / filename
                file.touch()
                with self.assertRaises(ValueError): ci.assert_public(stage)
                file.unlink()
            ci.assert_public(stage)

    def test_offscreen_runtime(self):
        env = ci.runtime_environment()
        self.assertEqual(env['QT_QPA_PLATFORM'], 'offscreen')
        self.assertEqual(env['QT_QUICK_BACKEND'], 'software')

    def test_standalone_cleans_developer_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(ci, 'run') as run:
                run.return_value.stdout = ''
                run.return_value.stderr = ''
                env = ci.runtime_environment()
                env.update(QT_PLUGIN_PATH='private-qt', QML_IMPORT_PATH='private-qml',
                           IPR_RAW_NORMALIZATION_MANIFEST='private.json')
                ci.smoke(Path(directory), 'standalone', env)
                child = run.call_args.kwargs['env']
                self.assertNotIn('QT_PLUGIN_PATH', child)
                self.assertNotIn('IPR_RAW_NORMALIZATION_MANIFEST', child)
                self.assertNotEqual(run.call_args.kwargs['cwd'], str(ROOT))

    def test_manifest_dependencies(self):
        manifest = json.loads((ROOT / 'vcpkg.json').read_text())
        self.assertEqual(len(manifest['builtin-baseline']), 40)
        self.assertIn('libraw', manifest['dependencies'])

    def test_workflow_contracts(self):
        import yaml
        for path in (ROOT / '.github/workflows').glob('*.yml'):
            workflow = yaml.load(path.read_text(), Loader=yaml.BaseLoader)
            self.assertEqual(workflow['permissions'], {'contents': 'read'})
            self.assertNotIn('pull_request_target', workflow['on'])
            for job in workflow['jobs'].values():
                for step in job['steps']:
                    action = step.get('uses', '')
                    if action and not action.startswith('./'):
                        self.assertRegex(action, r'@[a-f0-9]{40}$')
        delivery = yaml.load((ROOT / '.github/workflows/delivery.yml').read_text(), Loader=yaml.BaseLoader)
        matrix = delivery['jobs']['archives']['strategy']['matrix']
        self.assertEqual(set(matrix['mode']), {'system', 'standalone'})
        self.assertEqual(len(matrix['os']), 4)


if __name__ == '__main__': unittest.main()
