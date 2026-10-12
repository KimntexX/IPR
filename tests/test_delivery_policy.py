"""Delivery authorization and packaging regressions, without private photos."""
import json
import os
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci
import delivery_policy as policy

SHA = 'a' * 40
REPO = 'KimntexX/IPR'


def run_record():
    return {'id': 123, 'conclusion': 'success', 'status': 'completed',
            'head_sha': SHA, 'head_branch': 'dev', 'event': 'push',
            'path': '.github/workflows/ci.yml', 'head_repository': {'full_name': REPO}}


def jobs():
    return [{'name': f'native ({name})', 'conclusion': 'success'} for name in policy.PLATFORMS]


class DeliveryPolicyTests(unittest.TestCase):
    def test_only_dev_main_manual_dispatch(self):
        for branch in ('dev', 'main'):
            self.assertEqual(policy.context('workflow_dispatch', branch, SHA, {}, REPO)[:2], (branch, SHA))
        for branch in ('patch_ci_cd', 'patch_color', 'v0.1.17-alpha'):
            with self.assertRaises(ValueError):
                policy.context('workflow_dispatch', branch, SHA, {}, REPO)
        with self.assertRaises(ValueError):
            policy.context('push', 'dev', SHA, {}, REPO)

    def test_completion_uses_trigger_commit_not_current_branch(self):
        payload = {'workflow_run': run_record()}
        self.assertEqual(policy.context('workflow_run', 'main', 'b' * 40, payload, REPO), ('dev', SHA, 123))
        payload['workflow_run']['head_repository']['full_name'] = 'fork/IPR'
        with self.assertRaises(ValueError):
            policy.context('workflow_run', 'main', SHA, payload, REPO)

    def test_all_four_platforms_and_exact_success_required(self):
        policy.validate_ci(run_record(), jobs(), 'dev', SHA, REPO)
        for key, value in [('head_sha', 'b' * 40), ('head_branch', 'patch_color'),
                           ('event', 'pull_request'), ('conclusion', 'failure'),
                           ('status', 'in_progress'), ('path', '.github/workflows/delivery.yml')]:
            record = dict(run_record(), **{key: value})
            with self.assertRaises(ValueError):
                policy.validate_ci(record, jobs(), 'dev', SHA, REPO)
        with self.assertRaises(ValueError):
            policy.validate_ci(run_record(), jobs()[:-1], 'dev', SHA, REPO)
        with self.assertRaises(ValueError):
            policy.validate_ci(run_record(), jobs() + [{'name': 'CI validation', 'conclusion': 'skipped'}], 'dev', SHA, REPO)

    def test_environment_checks_current_checkout_and_ci_api(self):
        with tempfile.TemporaryDirectory() as directory:
            event = Path(directory) / 'event.json'
            event.write_text('{}', encoding='utf8')
            env = {'GITHUB_EVENT_NAME': 'workflow_dispatch', 'GITHUB_REF_NAME': 'dev',
                   'GITHUB_SHA': SHA, 'GITHUB_REPOSITORY': REPO, 'GITHUB_EVENT_PATH': str(event)}
            with patch.dict(os.environ, env, clear=True), patch.object(policy.subprocess, 'check_output', side_effect=[SHA, '']), patch.object(policy, 'api', side_effect=[{'workflow_runs': [run_record()]}, {'jobs': jobs()}]):
                self.assertEqual(policy.verify_publication_environment()['sha'], SHA)
            for outputs in ([('b' * 40)], [SHA, ' M source.cpp']):
                with patch.dict(os.environ, env, clear=True), patch.object(policy.subprocess, 'check_output', side_effect=outputs), self.assertRaises(ValueError):
                    policy.verify_publication_environment()

    def test_runtime_dll_search_paths_are_absolute(self):
        env = ci.runtime_environment(runtime_dirs=['build-ci/vcpkg_installed/x64-windows/bin'])
        self.assertEqual(env['PATH'].split(os.pathsep)[0], str((Path.cwd() / 'build-ci/vcpkg_installed/x64-windows/bin').resolve()))

    def test_mac_deploy_is_single_pass_with_scanned_qml(self):
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            contents = stage / 'appIPR.app/Contents'
            (contents / 'MacOS').mkdir(parents=True)
            (contents / 'MacOS/appIPR').touch()
            with (contents / 'Info.plist').open('wb') as stream:
                plistlib.dump({'CFBundleExecutable': 'appIPR'}, stream)
            with patch.object(ci, 'run') as run:
                run.return_value.stdout = run.return_value.stderr = ''
                ci.deploy_macos(stage, {'QT_ROOT_DIR': '/qt'})
                self.assertEqual(run.call_count, 1)
                self.assertEqual(run.call_args.kwargs['cwd'], stage)
                self.assertIn('-qmldir=' + str(ci.ROOT / 'src/qml'), run.call_args.args[0])
                run.return_value.stdout = 'ERROR: Cannot resolve framework'
                with self.assertRaises(RuntimeError):
                    ci.deploy_macos(stage, {'QT_ROOT_DIR': '/qt'})

    def test_verify_only_creates_no_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'output'
            def install(command, **kwargs):
                stage = Path(command[-1])
                stage.mkdir(parents=True)
            with patch.object(ci, 'run', side_effect=install), patch.object(ci.platform, 'system', return_value='Linux'), patch.object(ci, 'platform_name', return_value='linux-x64'), patch.object(ci, 'smoke', return_value={'startup': 'passed'}), patch.object(ci, 'cache', return_value={}), patch.object(ci.subprocess, 'check_output', side_effect=[SHA, '']):
                ci.package(Path(directory), output, '0.1.17-alpha', 'system', {}, [], verify_only=True)
            self.assertFalse(list(output.glob('*.zip')))
            self.assertFalse(list(output.glob('*.tar.gz')))
            self.assertEqual(len(list(output.rglob('package-manifest.json'))), 1)
            manifest = json.loads(next(output.rglob('package-manifest.json')).read_text())
            self.assertFalse(manifest['downloadable_archive_requested'])
            self.assertIsNone(manifest['github_delivery_approval'])

    def test_mac_standalone_rejects_homebrew_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'appIPR'
            binary.write_bytes(b'\xcf\xfa\xed\xfe')
            with patch.object(ci.subprocess, 'check_output', return_value='appIPR:\n\t/usr/lib/libSystem.B.dylib (compatibility version 1)\n'):
                ci.assert_macos_portable(Path(directory))
            with patch.object(ci.subprocess, 'check_output', return_value='appIPR:\n\t/opt/homebrew/lib/libraw.dylib (compatibility version 1)\n'), self.assertRaises(ValueError):
                ci.assert_macos_portable(Path(directory))

    def test_linux_deployment_has_real_build_executable(self):
        script = (ROOT / 'cmake/Deployment.cmake').read_text()
        self.assertIn('$<TARGET_FILE:appIPR>', script)
        self.assertNotIn('qt_generate_deploy_qml_app_script(TARGET appIPR OUTPUT_SCRIPT ipr_deploy\n            DEPLOY_TOOL_OPTIONS -always-overwrite)', script)

    def test_workflows_cannot_publish_from_patch_or_pr(self):
        import yaml
        load = lambda filename: yaml.load((ROOT / '.github/workflows' / filename).read_text(), Loader=yaml.BaseLoader)
        delivery = load('delivery.yml')
        self.assertNotIn('push', delivery['on'])
        self.assertEqual(delivery['on']['workflow_run']['branches'], ['dev', 'main'])
        for filename in ('delivery.yml', 'linux-distributions.yml'):
            workflow = load(filename)
            self.assertIn('refs/heads/dev', workflow['jobs']['approval']['if'])
            self.assertIn('refs/heads/main', workflow['jobs']['approval']['if'])
            job = workflow['jobs']['archives' if filename == 'delivery.yml' else 'distro']
            self.assertEqual(job['needs'], 'approval')
            checkout = next(step for step in job['steps'] if step.get('uses', '').startswith('actions/checkout@'))
            self.assertEqual(checkout['with']['ref'], '${{ needs.approval.outputs.sha }}')
        validation = load('ci.yml')
        verification = validation['jobs']['packaging-check']
        self.assertIn('refs/heads/patch_ci_cd', verification['if'])
        self.assertTrue(any('--verify-package-only' in s.get('run', '') for s in verification['steps']))
        for job in validation['jobs'].values():
            for step in job['steps']:
                if step.get('uses', '').startswith('actions/upload-artifact@'):
                    self.assertTrue(step['with']['name'].startswith('test-logs-'))
                    self.assertNotIn('dist/', step['with']['path'])


if __name__ == '__main__':
    unittest.main()
