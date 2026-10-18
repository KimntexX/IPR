import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import ci_scope
import ci_documents
import ci

RULES = json.loads((ROOT / 'scripts/test-rules.json').read_text(encoding='utf8'))


class ScopeTests(unittest.TestCase):
    def scope(self, paths, event='push', branch='patch_color', available=True):
        return ci_scope.plan_scope(paths, event, branch, RULES, available)

    def test_documentation_avoids_native_and_python(self):
        for branch in ('patch_documentation', 'patch_color'):
            plan = self.scope(['docs/README_en.md', 'README_en.md', 'docs/asset/ipr.png'], branch=branch)
            self.assertFalse(plan['native'])
            self.assertFalse(plan['python'])
            self.assertFalse(plan['full'])
            self.assertTrue(plan['documents'])

    def test_documentary_name_does_not_hide_code(self):
        plan = self.scope(['src/core/ToneRecovery.h'], branch='patch_documentation')
        self.assertTrue(plan['native'])
        self.assertEqual(plan['tests'], ['imageExportTests', 'toneRecoveryTests'])
        self.assertNotIn('macos-15-intel', plan['matrix']['os'])

    def test_preferences_components_use_their_focused_qml_tests(self):
        for path in ('src/qml/PreferencesDialog.qml', 'src/qml/PreferenceButton.qml',
                     'src/qml/AppearancePreferencesPane.qml', 'src/qml/RawCalibrationModule.qml',
                     'src/qml/ImageBackgroundOptions.qml', 'src/qml/ImageCanvasBackground.qml',
                     'src/qml/ChevronIcon.qml', 'src/qml/FilmstripNavigation.qml'):
            plan = self.scope([path], branch='patch_ui')
            self.assertFalse(plan['full'])
            self.assertEqual(plan['tests'], ['uiPreferencesQmlTests'])

    def test_python_only_needs_no_native_dependencies(self):
        plan = self.scope(['tests/test_investigate_wb_adaptation.py'])
        self.assertFalse(plan['native'])
        self.assertEqual(plan['python'], ['wb_adaptation'])

    def test_full_gates(self):
        for event, branch in [('pull_request', 'patch_color'), ('workflow_dispatch', 'patch_color'),
                              ('push', 'dev'), ('push', 'main')]:
            plan = self.scope(['README.md'], event, branch)
            self.assertTrue(plan['full'])
            self.assertTrue(plan['native'])
            self.assertIn('macos-15-intel', plan['matrix']['os'])

    def test_conservative_fallbacks(self):
        for paths in (['src/new.cpp'], ['CMakeLists.txt'], ['.github/workflows/ci.yml'],
                      [f'src/new{i}.cpp' for i in range(20)]):
            self.assertTrue(self.scope(paths)['full'])
        self.assertTrue(self.scope([], available=False)['full'])

    def test_no_changes_skips_native(self):
        self.assertFalse(self.scope([])['native'])

    def test_cli_publishes_plan_and_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'plan.json'
            github_output = Path(directory) / 'github-output'
            env = {'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF_NAME': 'patch_documentation',
                   'GITHUB_OUTPUT': str(github_output)}
            with patch.dict(os.environ, env, clear=True), patch.object(ci_scope.subprocess, 'check_output', return_value='base\n'), patch.object(ci_scope, 'changed_paths', return_value=['docs/README.md']):
                self.assertEqual(ci_scope.main(['--output', str(output)]), 0)
            plan = json.loads(output.read_text())
            self.assertFalse(plan['native'])
            self.assertIn('native=false', github_output.read_text())
            self.assertIn('documents=true', github_output.read_text())

    def test_cli_missing_history_is_full(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'plan.json'
            with patch.dict(os.environ, {'GITHUB_EVENT_NAME': 'push', 'GITHUB_REF_NAME': 'patch_color'}, clear=True), patch.object(ci_scope.subprocess, 'check_output', side_effect=subprocess.CalledProcessError(1, 'git')):
                self.assertEqual(ci_scope.main(['--output', str(output)]), 0)
            self.assertTrue(json.loads(output.read_text())['full'])

    def test_renames_include_deleted_and_added_paths(self):
        with patch.object(subprocess, 'check_output', return_value=b'src/old.cpp\0src/new.cpp\0') as command:
            self.assertEqual(ci_scope.changed_paths('base', 'head'), ['src/old.cpp', 'src/new.cpp'])
            self.assertIn('--no-renames', command.call_args.args[0])

    def test_plan_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'plan.json'
            plan = self.scope(['src/core/ToneRecovery.h'])
            path.write_text(json.dumps(plan), encoding='utf8')
            self.assertEqual(ci.load_test_plan(path), plan)
            for key, value in [('tests', ['notATest']), ('python', ['missing']),
                               ('targets', ['appIPR']), ('full', 'false')]:
                invalid = dict(plan, **{key: value})
                path.write_text(json.dumps(invalid), encoding='utf8')
                with self.assertRaises(ValueError):
                    ci.load_test_plan(path)

    def test_python_targeted_and_default_full(self):
        with patch.object(ci, 'run') as run:
            ci.run_python_tests(self.scope(['tests/test_investigate_wb_adaptation.py']), {})
            self.assertEqual(run.call_args.args[0][-1], 'test_investigate_wb_adaptation.py')
            ci.run_python_tests(None, {})
            self.assertEqual(run.call_args.args[0][-1], 'test_*.py')

    def test_exact_ctest_selection_and_missing_test(self):
        plan = self.scope(['src/core/ToneRecovery.h'])
        registered = {'tests': [{'name': name} for name in plan['tests']]}
        with patch.object(ci.subprocess, 'check_output', return_value=json.dumps(registered)), patch.object(ci, 'run') as run:
            ci.run_native_tests(ROOT / 'build', ROOT, plan, {})
            self.assertEqual(run.call_args.args[0][-2:], ['-R', '^(imageExportTests|toneRecoveryTests)$'])
        with patch.object(ci.subprocess, 'check_output', return_value='{"tests": []}'):
            with self.assertRaises(ValueError):
                ci.run_native_tests(ROOT / 'build', ROOT, plan, {})

    def test_delivery_cannot_be_scoped(self):
        with self.assertRaises(SystemExit):
            ci.main(['--action', 'all', '--test-plan', 'does-not-exist.json'])

    def test_readonly_documents_and_invalid_json(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'docs').mkdir()
            file = root / 'docs/test.md'
            file.write_text('[pending](pending.md)', encoding='utf8')
            before = file.read_bytes()
            self.assertEqual(ci_documents.check(root), 0)
            self.assertEqual(file.read_bytes(), before)
            (root / 'docs/test.json').write_text('{broken', encoding='utf8')
            self.assertEqual(ci_documents.check(root), 1)

    def test_workflow_gates(self):
        import yaml
        workflow = yaml.load((ROOT / '.github/workflows/ci.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(workflow['on']['pull_request']['branches'], ['main', 'dev'])
        self.assertNotIn('paths', workflow['on']['pull_request'])
        self.assertEqual(workflow['jobs']['validation']['if'], 'always()')
        self.assertEqual(workflow['jobs']['validation']['needs'], ['scope', 'documents', 'python', 'native'])
        self.assertIn('fromJSON', workflow['jobs']['native']['strategy']['matrix'])
        delivery = yaml.load((ROOT / '.github/workflows/delivery.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertNotIn('push', delivery['on'])
        self.assertEqual(delivery['on']['workflow_run']['branches'], ['dev', 'main'])


if __name__ == '__main__':
    unittest.main()
