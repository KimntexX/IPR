import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('windows_installer', ROOT / 'scripts/windows_installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def fixture(stage):
    for name in ('bin/appIPR.exe', 'plugins/platforms/qwindows.dll', 'bin/config/ipr-ui.ini'):
        path = stage / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'test fixture, not executable')
    path = stage / 'bin/profiles/index.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('{"version":1,"entries":[]}', encoding='utf8')
    manifest = dict(version='0.1.20-alpha', mode='standalone', private_profiles=False,
                    smoke=dict(startup='passed'), files=[dict(path=p.relative_to(stage).as_posix(),
                    bytes=p.stat().st_size, sha256=installer.digest(p)) for p in stage.rglob('*') if p.is_file()])
    (stage / 'package-manifest.json').write_text(json.dumps(manifest), encoding='utf8')


class WindowsInstallerTests(unittest.TestCase):
    def test_release_labels(self):
        for label in ('0.1.20-alpha', '0.2.1-beta', '0.10.2-beta', '1.0.0+dev.abcdef'):
            self.assertIsNotNone(installer.LABEL.fullmatch(label))
        for label in ('0.1.20', '0.1.20-alpha\n', '0.2.1-alpha', '../unsafe'):
            self.assertIsNone(installer.LABEL.fullmatch(label))

    def test_install_smoke_never_runs_on_developer_pc(self):
        with patch.dict(installer.os.environ, {'GITHUB_ACTIONS': 'false'}):
            with self.assertRaises(RuntimeError):
                installer.exercise_installer(Path('never-execute.exe'), Path('.'))

    def test_installed_preferences_not_in_inventory(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            fixture(stage)
            manifest = installer.installed_manifest(stage, '0.1.20-alpha')
            self.assertNotIn('bin/config/ipr-ui.ini', [p['path'] for p in manifest['files']])
            self.assertTrue((stage / 'bin/config/ipr-ui.ini').exists())
            self.assertEqual(manifest['distribution'], 'windows-installer-per-user')

    def test_tampered_payload_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            fixture(stage)
            (stage / 'bin/appIPR.exe').write_bytes(b'changed')
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '0.1.20-alpha')

    def test_extra_private_file_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            fixture(stage)
            (stage / 'bin/private.dcp').touch()
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '0.1.20-alpha')

    def test_version_and_system_mode_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            fixture(stage)
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '../../unsafe')
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '0.1.21-alpha')
            path = stage / 'package-manifest.json'
            manifest = json.loads(path.read_text())
            manifest['mode'] = 'system'
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '0.1.20-alpha')

    def test_failed_smoke_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp)
            fixture(stage)
            path = stage / 'package-manifest.json'
            manifest = json.loads(path.read_text())
            manifest['smoke']['startup'] = 'failed'
            path.write_text(json.dumps(manifest))
            with self.assertRaises(ValueError): installer.installed_manifest(stage, '0.1.20-alpha')

    def test_delivery_keeps_branch_restriction(self):
        import yaml
        workflow = yaml.load((ROOT / '.github/workflows/delivery.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(workflow['on']['workflow_run']['branches'], ['dev', 'main'])
        packaging = (ROOT / '.github/workflows/ci.yml').read_text()
        self.assertIn('--verify-package-only "${installer[@]}"', packaging)
        self.assertNotIn('dist/*-setup.exe', packaging)

    def test_recipe_preserves_personal_files(self):
        recipe = (ROOT / 'packaging/windows/ipr.iss').read_text(encoding='utf8')
        self.assertIn('PrivilegesRequired=lowest', recipe)
        self.assertIn('DefaultDirName={localappdata}\\Programs\\IPR', recipe)
        self.assertIn('bin\\config\\ipr-ui.ini,package-manifest.json', recipe)
        self.assertNotIn('[UninstallDelete]', recipe)
        self.assertNotIn('[InstallDelete]', recipe)

    def test_compiler_validation_leaves_no_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage, output = Path(tmp) / 'stage', Path(tmp) / 'out'
            stage.mkdir()
            fixture(stage)
            def compile_fake(command, **kwargs):
                target = Path(next(s[2:] for s in command if s.startswith('/O')))
                (target / 'IPR-0.1.20-alpha-windows-x64-setup.exe').write_bytes(b'not an executable')
            with (patch.object(installer, 'find_compiler', return_value=Path('ISCC.exe')),
                  patch.object(installer.subprocess, 'run', side_effect=compile_fake)):
                self.assertIsNone(installer.build_installer(stage, output, '0.1.20-alpha', verify_only=True))
            self.assertEqual(list(output.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
