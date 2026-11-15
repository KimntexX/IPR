import importlib.util
import json
import os
from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import MagicMock, patch

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
    def test_uninstaller_waits_for_the_tree_without_interpolating_paths(self):
        command = ['C:/path with spaces/unins000.exe', '/LOG=C:/path with spaces/uninstall.log', '/NORESTART']
        process = MagicMock()
        process.wait.return_value = 0
        popen = MagicMock()
        popen.return_value.__enter__.return_value = process
        job_context = MagicMock()
        job = job_context.__enter__.return_value
        job.event_name = 'fixture-event'
        with patch.object(installer.os, 'name', 'nt'), patch.object(installer.shutil, 'which', return_value='powershell.exe'), patch.object(installer.subprocess, 'Popen', popen), patch.object(installer, '_WindowsProcessJob', return_value=job_context), patch.object(installer.time, 'monotonic', return_value=10):
            installer.run_process_tree(command)
        arguments = popen.call_args.args[0]
        script = arguments[-1]
        self.assertIn('$info.UseShellExecute = $false', script)
        self.assertIn('$info.CreateNoWindow = $true', script)
        self.assertNotIn(command[0], script)
        self.assertEqual(popen.call_args.kwargs['env']['IPR_SMOKE_EXECUTABLE'], command[0])
        self.assertEqual(popen.call_args.kwargs['env']['IPR_SMOKE_ARGUMENTS'], installer.subprocess.list2cmdline(command[1:]))
        self.assertEqual(popen.call_args.kwargs['creationflags'], 0x08000000)
        process.wait.assert_called_once_with(timeout=120)
        job.attach_and_start.assert_called_once_with(process.pid)
        job.wait_empty.assert_called_once_with(120, ())
        self.assertEqual(popen.call_args.kwargs['env']['IPR_SMOKE_START_GATE'], 'fixture-event')

    def test_uninstaller_failure_and_timeout_are_not_ignored(self):
        for failure in ('exit', 'timeout'):
            process = MagicMock(pid=12345)
            if failure == 'exit':
                process.wait.return_value = 2
            else:
                process.wait.side_effect = [installer.subprocess.TimeoutExpired('fixture', 3), 0]
                process.poll.return_value = None
            popen = MagicMock()
            popen.return_value.__enter__.return_value = process
            error = installer.subprocess.CalledProcessError if failure == 'exit' else installer.subprocess.TimeoutExpired
            job_context=MagicMock()
            job=job_context.__enter__.return_value
            with self.subTest(failure=failure), patch.object(installer.os, 'name', 'nt'), patch.object(installer.shutil, 'which', return_value='powershell.exe'), patch.object(installer.subprocess, 'Popen', popen), patch.object(installer, '_WindowsProcessJob', return_value=job_context), self.assertRaises(error):
                installer.run_process_tree(['fixture.exe','/SILENT'], timeout=3)
            if failure == 'timeout':
                job.stop.assert_called_once_with(())
                process.kill.assert_called_once()
                self.assertEqual(process.wait.call_args.kwargs['timeout'],15)
            else:
                job.stop.assert_not_called()

    def test_missing_waiting_tool_is_not_silently_bypassed(self):
        with patch.object(installer.os, 'name', 'nt'), patch.object(installer.shutil, 'which', return_value=None), patch.object(installer.subprocess, 'Popen') as popen, self.assertRaises(FileNotFoundError):
            installer.run_process_tree(['fixture.exe','/SILENT'])
        popen.assert_not_called()

    @unittest.skipUnless(os.name == 'nt', 'Windows descendant-wait integration')
    def test_real_child_releases_log_before_temporary_cleanup(self):
        # No installation or registry changes: a short-lived Python launcher
        # leaves a child holding a log after the launcher has exited.
        with tempfile.TemporaryDirectory(prefix='ipr child wait ') as temporary:
            work = Path(temporary)
            child = work/'child.py'
            child.write_text('import sys,time\nfrom pathlib import Path\n'
                'with Path(sys.argv[1]).open("w") as log:\n'
                '    log.write("held"); log.flush(); time.sleep(0.6)\n'
                'Path(sys.argv[2]).write_text("closed")\n',encoding='utf8')
            launcher = work/'launcher.py'
            launcher.write_text('import subprocess,sys\n'
                'subprocess.Popen([sys.executable,*sys.argv[1:]])\n',encoding='utf8')
            log, marker = work/'uninstall.log', work/'finished.txt'
            installer.run_process_tree([sys.executable, str(launcher), str(child), str(log), str(marker)], timeout=20, completion_files=(log,))
            self.assertEqual(marker.read_text(), 'closed')
            log.unlink()  # Would fail with WinError 32 while the child held it.

    @unittest.skipUnless(os.name == 'nt', 'Windows process-tree timeout integration')
    def test_real_timeout_terminates_owned_child_and_releases_log(self):
        with tempfile.TemporaryDirectory(prefix='ipr child timeout ') as temporary:
            work = Path(temporary)
            child = work/'child.py'
            child.write_text('import sys,time\nfrom pathlib import Path\n'
                'with Path(sys.argv[1]).open("w") as log:\n'
                '    log.write("held"); log.flush(); time.sleep(30)\n',encoding='utf8')
            launcher = work/'launcher.py'
            launcher.write_text('import subprocess,sys\n'
                'subprocess.Popen([sys.executable,*sys.argv[1:]])\n',encoding='utf8')
            log = work/'uninstall.log'
            with self.assertRaises(installer.subprocess.TimeoutExpired):
                installer.run_process_tree([sys.executable, str(launcher), str(child), str(log)], timeout=5, completion_files=(log,))
            self.assertTrue(log.exists(), 'The held-log child must have run for this test to qualify cleanup')
            log.unlink()

    @unittest.skipUnless(os.name == 'nt', 'Windows exclusive log-handle integration')
    def test_log_handle_probe_detects_a_live_writer(self):
        with tempfile.TemporaryDirectory() as temporary, installer._WindowsProcessJob() as job:
            log=Path(temporary)/'uninstall.log'
            with log.open('w'):
                self.assertFalse(job.files_released((log,)))
            self.assertTrue(job.files_released((log,)))

    @unittest.skipUnless(os.name == 'nt', 'Windows process exit integration')
    def test_real_process_exit_code_is_propagated(self):
        with self.assertRaises(installer.subprocess.CalledProcessError) as error:
            installer.run_process_tree([sys.executable, '-c', 'import sys; sys.exit(7)'], timeout=20)
        self.assertEqual(error.exception.returncode, 7)

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
