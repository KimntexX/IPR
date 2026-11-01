"""Offline qualification of promotion gates and immutable release payloads."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import delivery_policy as policy
import promote_main
import release_github as release

REPO = 'KimntexX/IPR'
MAIN, DEV = 'a' * 40, 'b' * 40
APPROVAL = dict(branch='main', sha=MAIN, main_sha=MAIN, build_sha=DEV,
                dev_ci_run_id=11, ci_run_id=22, closure_sha256='c' * 64,
                allowlist_sha256='d' * 64)
PLAN = dict(schema=1, repository=REPO, tag='v0.1.80-alpha+main',
            version='0.1.80-alpha+main',
            published_configurations=json.loads(policy.matrix_value(policy.PACKAGE_MATRIX))['include'], **APPROVAL)


class MainGateTests(unittest.TestCase):
    def test_main_requires_all_production_jobs_and_aggregate(self):
        run = dict(id=22, conclusion='success', status='completed', head_sha=MAIN,
                   head_branch='main', event='push', path=policy.CI_WORKFLOWS['main'],
                   head_repository={'full_name': REPO})
        names = [f'production ({host}, {mode})' for host, mode in policy.PRODUCTION_MATRIX]
        names += ['Promotion verification', 'Main production validation']
        jobs = [dict(name=name, conclusion='success') for name in names]
        policy.validate_ci(run, jobs, 'main', MAIN, REPO)
        for index in range(len(jobs)):
            with self.subTest(missing=jobs[index]['name']), self.assertRaises(ValueError):
                policy.validate_ci(run, jobs[:index] + jobs[index + 1:], 'main', MAIN, REPO)
        for field, value in [('event', 'pull_request'), ('head_sha', DEV),
                             ('path', policy.CI_WORKFLOWS['dev'])]:
            with self.subTest(field=field), self.assertRaises(ValueError):
                policy.validate_ci(dict(run, **{field: value}), jobs, 'main', MAIN, REPO)

    def test_two_gates_are_independently_required(self):
        manifest = dict(schema=1, policy=promote_main.POLICY,
                        production_profile=promote_main.PRODUCTION_PROFILE,
                        closure_sha256=APPROVAL['closure_sha256'],
                        allowlist={'sha256': APPROVAL['allowlist_sha256']},
                        source=dict(repository='https://github.com/' + REPO, commit=DEV,
                                    ci=dict(run_id='11', workflow=policy.CI_WORKFLOWS['dev'])))
        with patch.object(policy, 'git', return_value=json.dumps(manifest)), patch.object(promote_main, 'verify', return_value=[]), patch.object(policy, 'require_ci', side_effect=[11, 22]) as gate:
            self.assertEqual(policy.authorize_main(MAIN, REPO, 22), APPROVAL)
            self.assertEqual(gate.call_args_list[0].args, ('dev', DEV, REPO, '11'))
            self.assertEqual(gate.call_args_list[1].args, ('main', MAIN, REPO, 22))
        for outcomes in ([ValueError('dev failed')], [11, ValueError('main failed')]):
            with patch.object(policy, 'git', return_value=json.dumps(manifest)), patch.object(promote_main, 'verify', return_value=[]), patch.object(policy, 'require_ci', side_effect=outcomes), self.assertRaises(ValueError):
                policy.authorize_main(MAIN, REPO)
        with patch.object(policy, 'git', return_value=json.dumps(manifest)), patch.object(promote_main, 'verify', return_value=['changed source']), patch.object(policy, 'require_ci') as gate, self.assertRaises(ValueError):
            policy.authorize_main(MAIN, REPO)
        gate.assert_not_called()

    def test_job_pagination(self):
        with patch.object(policy, 'api', side_effect=[{'jobs': [{}] * 100}, {'jobs': [{'name': 'last'}]}]) as api:
            self.assertEqual(len(policy.run_jobs(REPO, 22)), 101)
            self.assertIn('page=2', api.call_args.args[0])

    def test_main_planning_and_package_checkout_are_different(self):
        with tempfile.TemporaryDirectory() as directory:
            event = Path(directory) / 'event.json'
            event.write_text('{}', encoding='utf8')
            env = dict(GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF_NAME='main',
                       GITHUB_SHA=MAIN, GITHUB_REPOSITORY=REPO, GITHUB_EVENT_PATH=str(event))
            for planning, head in ((True, MAIN), (False, DEV)):
                with patch.dict(os.environ, env), patch.object(policy, 'authorize_main', return_value=APPROVAL), patch.object(policy, 'git', side_effect=[head, '']):
                    self.assertEqual(policy.verify_publication_environment(planning=planning), APPROVAL)
                with patch.dict(os.environ, env), patch.object(policy, 'authorize_main', return_value=APPROVAL), patch.object(policy, 'git', side_effect=[DEV if planning else MAIN, '']), self.assertRaises(ValueError):
                    policy.verify_publication_environment(planning=planning)


class ReleaseTests(unittest.TestCase):
    def test_optional_release_prefix_only_changes_version_parsing(self):
        for version in ('0.1.83-alpha', '0.2.1-beta', '1.0.0', '0.1.85-alpha+main'):
            for prefix in ('v', 'release-v'):
                tag = prefix + version
                with self.subTest(tag=tag):
                    self.assertEqual(release.version_from_tag(tag), version)
                    with patch.object(policy, 'api', return_value={'object': {'sha': MAIN, 'type': 'commit'}}) as api:
                        self.assertEqual(release.existing_tag_commit(REPO, tag), MAIN)
                        self.assertEqual(api.call_args.args[0],
                            'repos/' + REPO + '/git/ref/tags/' + release.quote(tag, safe=''))
        for tag in ('release-release-v0.1.83-alpha', 'release-0.1.83-alpha',
                    'release-v0.1.83', 'release-v0.1.83-alpha/other',
                    'release-v0.1.83-alpha\n', 'Release-v0.1.83-alpha',
                    'release-v0.1.83-alpha --force', 'release-v0.1.83-alpha?x=1'):
            with self.subTest(invalid_tag=tag), patch.object(policy, 'api') as api, self.assertRaises(ValueError):
                release.existing_tag_commit(REPO, tag)
            api.assert_not_called()

    def test_prefixed_plan_keeps_exact_main_target_and_version_checks(self):
        plan = dict(PLAN, tag='release-v0.1.83-alpha', version='0.1.83-alpha')
        with patch.dict(os.environ, GITHUB_REPOSITORY=REPO), patch.object(policy, 'authorize_main', return_value=APPROVAL), patch.object(policy, 'git', return_value='license'), patch.object(release, 'existing_tag_commit', return_value=MAIN) as tag:
            release.validate_plan(plan)
            tag.assert_called_with(REPO, 'release-v0.1.83-alpha')
            with patch.object(release, 'existing_tag_commit', return_value=DEV), self.assertRaises(ValueError):
                release.validate_plan(plan)
            with self.assertRaises(ValueError):
                release.validate_plan(dict(plan, version='0.1.84-alpha'))

    def test_planner_preserves_prefixed_tag_but_builds_numeric_release_label(self):
        env = dict(GITHUB_REPOSITORY=REPO, GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main')
        with patch.dict(os.environ, env), patch.object(policy, 'verify_publication_environment', return_value=APPROVAL), patch.object(policy, 'git', side_effect=lambda *args: MAIN if args[0] == 'rev-parse' else 'license'), patch.object(policy, 'authorize_main', return_value=APPROVAL), patch.object(release, 'existing_tag_commit', return_value=MAIN) as tag:
            plan = release.make_plan('release-v0.1.85-alpha')
        self.assertEqual(plan['tag'], 'release-v0.1.85-alpha')
        self.assertEqual(plan['version'], '0.1.85-alpha')
        self.assertEqual(plan['build_sha'], DEV)
        self.assertEqual(plan['main_sha'], MAIN)
        tag.assert_called_once_with(REPO, 'release-v0.1.85-alpha')

    def test_existing_annotated_tag_is_resolved_without_writes(self):
        with patch.object(policy, 'api', side_effect=[{'object': {'sha': DEV, 'type': 'tag'}}, {'object': {'sha': MAIN, 'type': 'commit'}}]) as api:
            self.assertEqual(release.existing_tag_commit(REPO, PLAN['tag']), MAIN)
            self.assertEqual(api.call_count, 2)
        with patch.object(policy, 'api') as api, self.assertRaises(ValueError):
            release.existing_tag_commit(REPO, '../../tag')
        api.assert_not_called()

    def test_plan_rechecks_both_gates_license_and_main_tag(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY=REPO), patch.object(policy, 'authorize_main', return_value=APPROVAL), patch.object(policy, 'git', return_value='license'), patch.object(release, 'existing_tag_commit', return_value=MAIN):
            release.validate_plan(PLAN)
            for key, value in [('repository', 'foreign/repo'), ('build_sha', MAIN), ('version', '0.1.1-alpha')]:
                with self.subTest(key=key), self.assertRaises(ValueError):
                    release.validate_plan(dict(PLAN, **{key: value}))
            with patch.object(release, 'existing_tag_commit', return_value=DEV), self.assertRaises(ValueError):
                release.validate_plan(PLAN)
            with patch.object(policy, 'git', return_value=''), self.assertRaises(ValueError):
                release.validate_plan(PLAN)

    def test_archive_metadata_is_not_extracted_and_paths_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / 'payload.zip'
            for unsafe in (False, True):
                with zipfile.ZipFile(archive, 'w') as stream:
                    stream.writestr('IPR/package-manifest.json', '{"schema": 1}')
                    if unsafe:
                        stream.writestr('../outside', 'no')
                if unsafe:
                    with self.assertRaises(ValueError):
                        release.archive_manifest(archive)
                else:
                    self.assertEqual(release.archive_manifest(archive), {'schema': 1})
            self.assertEqual([p.name for p in Path(directory).iterdir()], ['payload.zip'])

    def test_package_identity_and_both_approvals(self):
        package = dict(commit=DEV, source_dirty=False, version=PLAN['version'], mode='standalone',
                       platform='windows-x64', private_profiles=False,
                       smoke={'startup': 'passed'}, github_delivery_approval=APPROVAL)
        release.check_package(package, PLAN, 'windows-2022', 'standalone')
        for key, value in [('commit', MAIN), ('source_dirty', True), ('private_profiles', True),
                           ('smoke', {}), ('github_delivery_approval', {'ci_run_id': 22})]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.check_package(dict(package, **{key: value}), PLAN, 'windows-2022', 'standalone')

    def test_inventory_requires_production_cache_and_matching_checksums(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary) / 'dist'
            directory.mkdir()
            plan_path = Path(temporary) / 'plan.json'
            plan_path.write_text(json.dumps(PLAN), encoding='utf8')
            package = dict(commit=DEV, source_dirty=False, version=PLAN['version'], mode='standalone',
                           platform='windows-x64', private_profiles=False,
                           smoke={'startup': 'passed'}, github_delivery_approval=APPROVAL)
            archive = directory / 'IPR.zip'
            with zipfile.ZipFile(archive, 'w') as stream:
                stream.writestr('IPR/package-manifest.json', json.dumps(package))
            installer = directory / 'IPR-setup.exe'
            installer.write_bytes(b'installer')
            for file in (archive, installer):
                file.with_name(file.name + '.sha256').write_text(release.digest(file) + '  ' + file.name + '\n', encoding='ascii')
            state = dict(BUILD_TESTING='OFF', IPR_BUILD_TOOLS='OFF', IPR_DEPLOY_MODE='standalone', IPR_RELEASE_LABEL=PLAN['version'])
            with patch.object(release, 'validate_plan'), patch.object(policy, 'git', side_effect=lambda *args: DEV if args[0] == 'rev-parse' else ''), patch.object(release, 'cache', return_value=state):
                record = release.inventory(PLAN, plan_path, directory, Path(temporary), 'windows-2022', 'standalone')
                self.assertEqual(len(record['files']), 4)
                with patch.object(release, 'cache', return_value=dict(state, BUILD_TESTING='ON')), self.assertRaises(ValueError):
                    release.inventory(PLAN, plan_path, directory, Path(temporary), 'windows-2022', 'standalone')
                installer.write_bytes(b'changed')
                with self.assertRaises(ValueError):
                    release.inventory(PLAN, plan_path, directory, Path(temporary), 'windows-2022', 'standalone')

    def create_downloads(self, directory):
        plan_path = directory.parent / 'plan.json'
        plan_path.write_text(json.dumps(PLAN), encoding='utf8')
        for runner, mode in policy.PACKAGE_MATRIX:
            payload = directory / f'{runner}-{mode}.zip'
            payload.write_bytes(b'payload')
            record = dict(schema=1, runner=runner, mode=mode, tag=PLAN['tag'],
                          plan_sha256=release.digest(plan_path), approval=APPROVAL,
                          production_options=dict(BUILD_TESTING='OFF', IPR_BUILD_TOOLS='OFF',
                              IPR_DEPLOY_MODE=mode, IPR_RELEASE_LABEL=PLAN['version']),
                          smoke={'startup': 'passed'}, files=[dict(name=payload.name,
                              bytes=payload.stat().st_size, sha256=release.digest(payload))])
            (directory / f'release-inventory-{runner}-{mode}.json').write_text(json.dumps(record), encoding='utf8')
        return plan_path

    def test_collection_rejects_missing_changed_or_extra_payloads(self):
        for corruption in ('none', 'missing', 'changed', 'extra', 'path'):
            with self.subTest(corruption=corruption), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary) / 'downloads'
                directory.mkdir()
                plan_path = self.create_downloads(directory)
                if corruption == 'missing':
                    next(directory.glob('release-inventory-*.json')).unlink()
                elif corruption == 'changed':
                    next(directory.glob('*.zip')).write_bytes(b'changed')
                elif corruption == 'extra':
                    (directory / 'extra.exe').touch()
                elif corruption == 'path':
                    record_path = next(directory.glob('release-inventory-*.json'))
                    record = json.loads(record_path.read_text())
                    record['files'][0]['name'] = '../outside'
                    record_path.write_text(json.dumps(record), encoding='utf8')
                if corruption == 'none':
                    self.assertEqual(len(release.collect(PLAN, plan_path, directory)), len(policy.PACKAGE_MATRIX))
                else:
                    with self.assertRaises(ValueError):
                        release.collect(PLAN, plan_path, directory)

    def test_publish_requires_explicit_input_and_keeps_verified_bytes(self):
        for enabled in (False, True):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary) / 'downloads'
                directory.mkdir()
                plan_path = self.create_downloads(directory)
                event = Path(temporary) / 'event.json'
                event.write_text(json.dumps({'inputs': {'publish': str(enabled).lower(), 'tag': PLAN['tag']}}))
                env = dict(GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main',
                           GITHUB_SHA=MAIN, GITHUB_EVENT_PATH=str(event))
                response = HTTPError('url', 404, '', {}, None) if enabled else None
                with patch.dict(os.environ, env), patch.object(release, 'validate_plan') as validate, patch.object(policy, 'git', side_effect=[MAIN, '']), patch.object(policy, 'api', side_effect=response), patch.object(release.subprocess, 'run') as run:
                    if not enabled:
                        with self.assertRaises(ValueError):
                            release.publish(PLAN, plan_path, directory)
                        run.assert_not_called()
                        validate.assert_not_called()
                    else:
                        release.publish(PLAN, plan_path, directory)
                        self.assertEqual(validate.call_count, 2)
                        create, publish = [call.args[0] for call in run.call_args_list]
                        self.assertIn('--verify-tag', create)
                        self.assertIn('--draft', create)
                        self.assertEqual(publish[:4], ['gh', 'release', 'edit', PLAN['tag']])
                        self.assertIn('--draft=false', publish)
                        self.assertFalse(any('build' == part for part in create + publish))

    def test_workflows_use_main_only_dispatch_production_and_protected_publication(self):
        import yaml
        def load(name):
            return yaml.load((ROOT / '.github/workflows' / name).read_text(), Loader=yaml.BaseLoader)
        main = load('main.yml')
        self.assertEqual(main['on']['push']['branches'], ['main'])
        command = ' '.join(step.get('run', '') for step in main['jobs']['production']['steps'])
        self.assertIn('--profile production', command)
        self.assertIn('--verify-package-only', command)
        workflow = load('release.yml')
        self.assertEqual(set(workflow['on']), {'workflow_dispatch'})
        self.assertEqual(workflow['on']['workflow_dispatch']['inputs']['publish']['default'], 'false')
        self.assertEqual(workflow['jobs']['publish']['environment'], 'release')
        for name, job in workflow['jobs'].items():
            self.assertEqual(job['permissions']['contents'], 'write' if name == 'publish' else 'read')
        checkout = workflow['jobs']['archives']['steps'][0]
        self.assertEqual(checkout['with']['ref'], '${{ needs.approval.outputs.build_sha }}')
        self.assertEqual(main['jobs']['production']['strategy']['matrix'], '${{ fromJSON(needs.promotion.outputs.matrix) }}')
        self.assertEqual(workflow['jobs']['archives']['strategy']['matrix'], '${{ fromJSON(needs.approval.outputs.matrix) }}')

    def test_workflow_yaml_has_no_duplicate_mapping_keys(self):
        import yaml
        class UniqueLoader(yaml.BaseLoader):
            def construct_mapping(self,node,deep=False):
                keys=[self.construct_object(key,deep=deep) for key,_ in node.value]
                if len(keys)!=len(set(keys)):
                    raise ValueError('Duplicate YAML mapping key')
                return super().construct_mapping(node,deep=deep)
        for path in (ROOT/'.github').rglob('*.yml'):
            with self.subTest(path=path):
                yaml.load(path.read_text(),Loader=UniqueLoader)
        scope=yaml.load((ROOT/'.github/workflows/ci.yml').read_text(),Loader=UniqueLoader)
        self.assertEqual(set(scope['jobs']['packaging-check']['needs']),{'scope','validation'})

    def test_changed_release_target_plan_is_rejected_before_api(self):
        with patch.object(policy, 'authorize_main') as api, self.assertRaises(ValueError):
            release.validate_plan(dict(PLAN, published_configurations=PLAN['published_configurations'][:-1]))
        api.assert_not_called()

    def test_planning_job_outputs_only_the_attested_publication_matrix(self):
        with patch.object(release,'make_plan',return_value=PLAN), patch.object(release,'write_json'), patch.object(policy,'emit') as emit:
            self.assertEqual(release.main(['--action','plan','--tag',PLAN['tag'],'--output','unused.json']),0)
        self.assertEqual(json.loads(emit.call_args.args[0]['package_matrix'])['include'],PLAN['published_configurations'])

    def test_suspended_configuration_cannot_be_inventoried(self):
        for runner in ('macos-15', 'macos-15-intel'):
            for mode in ('system', 'standalone'):
                with self.subTest(runner=runner,mode=mode), patch.object(release, 'validate_plan'), patch.object(release, 'cache') as cache, self.assertRaises(ValueError):
                    release.inventory(PLAN, Path('unused'), Path('unused'), Path('unused'), runner, mode)
                cache.assert_not_called()

    def test_existing_release_or_changed_gate_never_overwrites_or_publishes(self):
        for existing in (True, False):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary) / 'downloads'
                directory.mkdir()
                plan_path = self.create_downloads(directory)
                event = Path(temporary) / 'event.json'
                event.write_text(json.dumps({'inputs': {'publish': 'true', 'tag': PLAN['tag']}}))
                env = dict(GITHUB_EVENT_NAME='workflow_dispatch', GITHUB_REF='refs/heads/main',
                           GITHUB_SHA=MAIN, GITHUB_EVENT_PATH=str(event))
                response = {} if existing else HTTPError('url', 404, '', {}, None)
                with patch.dict(os.environ, env), patch.object(release, 'validate_plan', side_effect=[None, ValueError('Gate changed')]), patch.object(policy, 'git', side_effect=[MAIN, '']), patch.object(policy, 'api', side_effect=[response]), patch.object(release.subprocess, 'run') as run, self.assertRaises(ValueError):
                    release.publish(PLAN, plan_path, directory)
                self.assertEqual(run.call_count, 0 if existing else 1)
                if not existing:
                    self.assertIn('--draft', run.call_args.args[0])


if __name__ == '__main__':
    unittest.main()
