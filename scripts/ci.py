"""Native CI build/tests and public deployment for Windows, Linux and macOS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
VERSION = re.compile(r"v?(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta))?(?:\+[A-Za-z0-9.-]+)?\Z")


def load_test_plan(path):
    if path is None:
        return None  # Delivery/manual runner defaults to the complete suite.
    plan = json.loads(path.read_text(encoding='utf8'))
    rules = json.loads((ROOT / 'scripts/test-rules.json').read_text(encoding='utf8'))
    if not isinstance(plan, dict) or plan.get('schema') != 1 or type(plan.get('full')) is not bool:
        raise ValueError('Invalid CI test plan')
    for key, allowed in [('tests', rules['targets']), ('python', rules['python_suites'])]:
        values = plan.get(key)
        if not isinstance(values, list) or any(not isinstance(v, str) or v not in allowed for v in values):
            raise ValueError('Unknown selection in CI plan: ' + key)
    expected = sorted({rules['targets'][test] for test in plan['tests']})
    if plan.get('targets') != expected:
        raise ValueError('CI build targets do not match selected tests')
    return plan


def run_python_tests(plan, env):
    rules = json.loads((ROOT / 'scripts/test-rules.json').read_text(encoding='utf8'))
    patterns = ['test_*.py'] if plan is None or plan['full'] else [rules['python_suites'][s] for s in plan['python']]
    for pattern in patterns:
        if not list((ROOT / 'tests').glob(pattern)):
            raise ValueError('Python test suite missing: ' + pattern)
        run([sys.executable, '-m', 'unittest', 'discover', '-s', 'tests', '-p', pattern], cwd=ROOT, env=env)


def run_native_tests(build, test_dir, plan, env):
    if plan is not None and not plan['full'] and not plan['tests']:
        return
    command = ['ctest', '--test-dir', build, '-C', 'Release', '--output-on-failure',
               '--no-tests=error', '--timeout', '180', '--output-junit', test_dir / 'ctest.xml']
    if plan is not None and not plan['full']:
        registered = json.loads(subprocess.check_output(
            ['ctest', '--test-dir', str(build), '-C', 'Release', '--show-only=json-v1'], env=env, text=True))
        names = {test['name'] for test in registered['tests']}
        missing = set(plan['tests']) - names
        if missing:
            raise ValueError('Selected CTest tests missing: ' + ', '.join(sorted(missing)))
        command += ['-R', '^(' + '|'.join(re.escape(test) for test in plan['tests']) + ')$']
    run(command, cwd=ROOT, env=env)


def run(command, **kwargs):
    print('+', ' '.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def release_version(label):
    match = VERSION.fullmatch(label)
    if not match:
        raise ValueError('Expected v0.1.X-alpha, v0.Y.X-beta or v1.Y.X (optional +build)')
    major, minor, patch = map(int, match.group(1, 2, 3))
    phase = match.group(4)
    if not ((major == 0 and minor == 1 and phase == 'alpha') or
            (major == 0 and minor >= 2 and phase == 'beta') or
            (major >= 1 and phase is None)):
        raise ValueError('Version is incompatible with IPR alpha/beta/stable rules')
    return label.removeprefix('v'), f'{major}.{minor}.{patch}'


def cache(build):
    result = {}
    for line in (build / 'CMakeCache.txt').read_text(encoding='utf8').splitlines():
        if not line.startswith(('#', '//')) and ':' in line and '=' in line:
            key, value = line.split('=', 1)
            result[key.split(':', 1)[0]] = value
    return result


def runtime_environment(qt_root=None, runtime_dirs=()):
    env = os.environ.copy()
    directories = [str(Path(directory).resolve()) for directory in runtime_dirs]
    if qt_root:
        qt_root = Path(qt_root)
        directories.insert(0, str(qt_root / 'bin'))
        if (qt_root / 'plugins').is_dir(): env['QT_PLUGIN_PATH'] = str(qt_root / 'plugins')
        if (qt_root / 'qml').is_dir(): env['QML_IMPORT_PATH'] = str(qt_root / 'qml')
    env['PATH'] = os.pathsep.join(directories + [env.get('PATH', '')])
    env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
               QT_QUICK_CONTROLS_STYLE='Basic')
    return env


def smoke(stage, mode, env):
    if platform.system() == 'Darwin':
        binary = stage / 'appIPR.app/Contents/MacOS/appIPR'
    else:
        binary = stage / 'bin' / ('appIPR.exe' if os.name == 'nt' else 'appIPR')
    child = env.copy()
    for key in ('IPR_RAW_NORMALIZATION_MANIFEST', 'IPR_ADOBE_CAMERARAW_DIR'):
        child.pop(key, None)
    if mode == 'standalone':
        for key in ('QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH',
                    'QT_ROOT_DIR', 'CMAKE_PREFIX_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH'):
            child.pop(key, None)
        if os.name == 'nt':
            system = Path(child.get('SystemRoot', 'C:/Windows'))
            child['PATH'] = os.pathsep.join(map(str, [system / 'System32', system]))
        else:
            child['PATH'] = '/usr/bin:/bin'
            child['LD_LIBRARY_PATH'] = str(stage / 'lib') if platform.system() == 'Linux' else ''
    try:
        with tempfile.TemporaryDirectory(prefix='ipr-package-smoke-') as directory:
            completed = run([binary, '--smoke-test'], cwd=directory, env=child,
                            capture_output=True, text=True, timeout=45)
    except subprocess.CalledProcessError as error:
        raise RuntimeError(f'Package startup failed (exit {error.returncode}): '
                           + (error.stdout or '') + (error.stderr or '')) from error
    output = completed.stdout + completed.stderr
    if re.search(r'failed to load|is not installed|Type .* unavailable|ReferenceError|Error:', output, re.I):
        raise RuntimeError('QML/runtime smoke errors: ' + output)
    print(output, flush=True)
    return {'startup': 'passed', 'platform': 'offscreen', 'cwd': 'outside repository',
            'developer_search_paths_removed': mode == 'standalone',
            'scope': 'QML startup only, not hardware/GPU/RAW/clean-machine certification'}


def assert_public(stage):
    private = [str(p) for p in stage.rglob('*') if p.suffix.lower() in ('.dcp', '.lcp', '.cr2', '.cr3')]
    private += [str(p) for p in stage.rglob('raw-normalization.json')]
    if private: raise ValueError('Private profiles/calibration in public package: ' + ', '.join(private))


def platform_name():
    machine = platform.machine().lower()
    arch = {'amd64': 'x64', 'x86_64': 'x64', 'aarch64': 'arm64'}.get(machine, machine)
    name = platform.system().lower()
    if name == 'linux':
        release = platform.freedesktop_os_release()
        name = release.get('ID', 'linux') + '-' + release.get('VERSION_ID', 'rolling')
    return name + '-' + arch


def rewrite_homebrew(binary):
    lines = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
    for line in lines:
        old = line.strip().split(' (', 1)[0]
        new = re.sub(r'/Cellar/([^/]+)/[^/]+/', r'/opt/\1/', old)
        if new != old:
            if not Path(new).exists(): raise ValueError('Missing Homebrew opt dependency: ' + new)
            run(['install_name_tool', '-change', old, new, binary])


def deploy_macos(stage, env):
    bundle = stage / 'appIPR.app'
    qt_root = Path(env['QT_ROOT_DIR'])
    # One deployment pass, from the bundle's parent. Scan only project imports;
    # deploying the whole Homebrew Qt tree pulled unrelated missing frameworks.
    completed = run([qt_root / 'bin/macdeployqt', 'appIPR.app',
                     '-executable=appIPR.app/Contents/MacOS/iprGenerateTests',
                     '-qmldir=' + str(ROOT / 'src/qml'),
                     '-libpath=' + str(qt_root / 'lib'), '-always-overwrite'],
                    cwd=stage, env=env, capture_output=True, text=True)
    output = completed.stdout + completed.stderr
    print(output, flush=True)
    if re.search(r'\bERROR:', output):
        raise RuntimeError('macdeployqt reported unresolved dependencies')
    with (bundle / 'Contents/Info.plist').open('rb') as stream:
        executable = plistlib.load(stream)['CFBundleExecutable']
    if not (bundle / 'Contents/MacOS' / executable).is_file():
        raise ValueError('Bundle executable missing: ' + executable)
    broken = [str(p.relative_to(bundle)) for p in bundle.rglob('*') if p.is_symlink() and not p.exists()]
    if broken:
        raise ValueError('Broken bundle links: ' + ', '.join(broken))


def assert_macos_portable(bundle):
    magic = {b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe', b'\xfe\xed\xfa\xcf',
             b'\xcf\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
             b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca'}
    for binary in bundle.rglob('*'):
        if not binary.is_file() or binary.is_symlink():
            continue
        with binary.open('rb') as stream:
            if stream.read(4) not in magic:
                continue
        lines = subprocess.check_output(['otool', '-L', str(binary)], text=True).splitlines()[1:]
        for line in lines:
            dependency = line.strip().split(' (', 1)[0]
            if dependency.startswith('/') and not dependency.startswith(('/System/Library/', '/usr/lib/')):
                raise ValueError(f'Standalone still uses an external library: {binary.name}: {dependency}')


def arch_recipe(output, label):
    recipe = output / 'arch-recipe'
    recipe.mkdir(exist_ok=False)
    source = recipe / 'ipr-source.tar'
    run(['git', 'archive', '--format=tar', '--prefix=IPR/', '-o', source, 'HEAD'], cwd=ROOT)
    template = (ROOT / 'packaging/arch/PKGBUILD.in').read_text(encoding='utf8')
    template = template.replace('@VERSION@', label.replace('-', '_').replace('+', '_'))
    template = template.replace('@LABEL@', label).replace('@SHA256@', digest(source))
    template = template.replace('@NUMERIC@', release_version(label)[1])
    (recipe / 'PKGBUILD').write_text(template, encoding='utf8')
    return recipe


def package(build, output, label, mode, env, runtime_dirs, native_format=None, verify_only=False, approval=None):
    if native_format and (mode != 'system' or platform.system() != 'Linux'):
        raise ValueError('Native packages require Linux system mode')
    name = 'IPR-' + label + '-' + platform_name() + '-' + mode
    stage = output / '.staging' / name
    archive = output / (name + ('.zip' if os.name == 'nt' else '.tar.gz'))
    if stage.exists() or archive.exists(): raise FileExistsError('Choose a fresh build/output; package already exists')
    output.mkdir(parents=True, exist_ok=True)
    run(['cmake', '--install', build, '--config', 'Release', '--prefix', stage], cwd=ROOT, env=env)
    if mode == 'standalone':
        if os.name == 'nt':
            if not runtime_dirs: raise ValueError('Windows standalone requires --runtime-dir for third-party DLLs')
            for directory in runtime_dirs:
                for dll in Path(directory).glob('*.dll'):
                    shutil.copy2(dll, stage / 'bin' / dll.name)
        if platform.system() != 'Darwin':
            shutil.copytree(stage / 'share/IPR/config', stage / 'bin/config')
            shutil.copytree(stage / 'share/IPR/profiles', stage / 'bin/profiles')
    if platform.system() == 'Darwin':
        if mode == 'standalone':
            if 'QT_ROOT_DIR' not in env: raise ValueError('macOS standalone requires QT_ROOT_DIR')
            deploy_macos(stage, env)
        else:
            for binary in (stage / 'appIPR.app/Contents/MacOS').iterdir():
                if binary.is_file(): rewrite_homebrew(binary)
    assert_public(stage)
    licenses = stage / ('appIPR.app/Contents/Resources/licenses' if platform.system() == 'Darwin' else 'share/IPR/licenses')
    for directory in runtime_dirs:
        vcpkg_share = Path(directory).parent / 'share'
        if vcpkg_share.is_dir():
            for notice in vcpkg_share.glob('*/copyright'):
                destination = licenses / ('vcpkg-' + notice.parent.name + '.txt')
                shutil.copy2(notice, destination)
    if mode == 'standalone':
        qt_root = env.get('QT_ROOT_DIR')
        if qt_root:
            for folder in ('Licenses', 'LICENSES'):
                qt_notices = Path(qt_root) / folder
                if qt_notices.is_dir(): shutil.copytree(qt_notices, licenses / 'Qt', dirs_exist_ok=True)
        if platform.system() == 'Linux':
            for folder in Path('/usr/share/doc').glob('*'):
                if folder.name.startswith(('libopencv', 'libraw', 'libtiff', 'libjpeg', 'libpng', 'libwebp', 'zlib')):
                    if (folder / 'copyright').exists():
                        shutil.copy2(folder / 'copyright', licenses / (folder.name + '.txt'))
    if platform.system() == 'Darwin':
        if mode == 'standalone':
            assert_macos_portable(stage / 'appIPR.app')
        run(['codesign', '--force', '--deep', '--sign', '-', stage / 'appIPR.app'])
        run(['codesign', '--verify', '--deep', '--strict', stage / 'appIPR.app'])
    check = smoke(stage, mode, env)
    files = [{'path': p.relative_to(stage).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p)}
             for p in sorted(stage.rglob('*')) if p.is_file()]
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=ROOT, text=True))
    manifest = {'schema_version': 1, 'created_at_utc': datetime.now(timezone.utc).isoformat(),
                'version': label, 'commit': commit, 'mode': mode, 'platform': platform_name(),
                'downloadable_archive_requested': not verify_only,
                'github_delivery_approval': approval,
                'source_dirty': dirty,
                'private_profiles': False, 'signed_release': False, 'smoke': check,
                'build_dependencies': {k: v for k, v in cache(build).items()
                    if k in ('Qt6_DIR', 'OpenCV_DIR', 'LIBRAW_LIBRARY', 'TIFF_LIBRARY_RELEASE', 'CMAKE_CXX_COMPILER')},
                'files': files}
    (stage / 'package-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf8')
    if verify_only:
        print('Packaging verification passed; no downloadable archive created.', flush=True)
        return
    if os.name == 'nt':
        shutil.make_archive(str(archive.with_suffix('')), 'zip', root_dir=stage.parent, base_dir=stage.name)
    else:
        with tarfile.open(archive, 'w:gz') as stream: stream.add(stage, arcname=name)
    packages = [archive]
    if native_format in ('deb', 'rpm'):
        if mode != 'system' or platform.system() != 'Linux': raise ValueError('Native packages require Linux system mode')
        before = set(output.iterdir())
        run(['cpack', '--config', build / 'CPackConfig.cmake', '-G', native_format.upper(), '-B', output], cwd=ROOT, env=env)
        packages += sorted(p for p in set(output.iterdir()) - before if p.suffix in ('.deb', '.rpm'))
        if len(packages) == 1: raise RuntimeError('CPack produced no native package')
    elif native_format == 'arch':
        recipe = arch_recipe(output, label)
        if os.environ.get('GITHUB_OUTPUT'):
            with open(os.environ['GITHUB_OUTPUT'], 'a', encoding='utf8') as stream:
                stream.write('arch_recipe=' + str(recipe) + '\n')
    for path in packages:
        path.with_name(path.name + '.sha256').write_text(digest(path) + '  ' + path.name + '\n', encoding='ascii')
    print('Package:', archive, flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action', choices=('build', 'test', 'python', 'package', 'all'), default='all')
    parser.add_argument('--test-plan', type=Path, help='Scope produced by ci_scope.py; omitted means full tests')
    parser.add_argument('--verify-package-only', action='store_true',
                        help='Exercise deployment/smoke but create no archive; for CI packaging tests')
    parser.add_argument('--mode', choices=('system', 'standalone'), default='system')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-ci')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    parser.add_argument('--version', default='0.1.0-alpha')
    parser.add_argument('--qt-root', type=Path, default=os.environ.get('QT_ROOT_DIR'))
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--cmake-arg', action='append', default=[])
    parser.add_argument('--native', choices=('deb', 'rpm', 'arch'))
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args(argv)
    if args.verify_package_only and (args.native or args.action not in ('all', 'package')):
        parser.error('Packaging verification requires all/package without native distro archives')
    if args.test_plan and args.action in ('all', 'package'):
        parser.error('Delivery packaging may not use a targeted test plan')
    plan = load_test_plan(args.test_plan)
    label, numeric = release_version(args.version)
    if args.jobs < 1: parser.error('--jobs must be positive')
    build = args.build_dir.resolve()
    env = runtime_environment(args.qt_root, args.runtime_dir)
    if args.qt_root: env['QT_ROOT_DIR'] = str(args.qt_root.resolve())
    if args.action in ('build', 'all'):
        command = ['cmake', '-S', ROOT, '-B', build, '-DBUILD_TESTING=ON', '-DIPR_ENABLE_LIBRAW=ON',
                   '-DCMAKE_BUILD_TYPE=Release', '-DIPR_DEPLOY_MODE=' + args.mode,
                   '-DIPR_RELEASE_LABEL=' + label, '-DIPR_RELEASE_VERSION=' + numeric,
                   '-DCMAKE_INSTALL_LIBDIR=lib', '-DCMAKE_INSTALL_BINDIR=bin', '-DCMAKE_INSTALL_DATADIR=share']
        if args.qt_root: command.append('-DCMAKE_PREFIX_PATH=' + str(args.qt_root))
        if platform.system() == 'Linux': command.append('-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib')
        if not (build / 'CMakeCache.txt').exists():
            command += ['-G', 'Visual Studio 17 2022', '-A', 'x64'] if os.name == 'nt' else ['-G', 'Ninja']
        run(command + args.cmake_arg, cwd=ROOT, env=env)
        command = ['cmake', '--build', build, '--config', 'Release', '-j', args.jobs]
        if plan is not None and not plan['full']:
            if not plan['targets']:
                raise ValueError('A native build requires selected targets')
            command += ['--target'] + plan['targets']
        run(command, cwd=ROOT, env=env)
    if args.action in ('test', 'all'):
        test_dir = build / 'test-results'
        test_dir.mkdir(parents=True, exist_ok=True)
        run_native_tests(build, test_dir, plan, env)
    if args.action in ('test', 'python', 'all'):
        # Full native integrations exercise generator -> group analyser on a
        # tiny temporary fixture. Python-only scopes do not use a stale binary.
        if args.action in ('test', 'all') and (plan is None or plan['full']):
            generator = build / ('Release/iprGenerateTests.exe' if os.name == 'nt' else 'iprGenerateTests')
            if generator.is_file():
                env['IPR_GENERATOR_TEST_BIN'] = str(generator)
        run_python_tests(plan, env)
    if args.action in ('package', 'all'):
        state = cache(build)
        if state.get('IPR_DEPLOY_MODE') != args.mode: raise ValueError('Build/package mode mismatch')
        if state.get('IPR_RELEASE_LABEL') != label: raise ValueError('Build/package version mismatch')
        approval = None
        if os.environ.get('GITHUB_ACTIONS') == 'true' and not args.verify_package_only:
            from delivery_policy import verify_publication_environment
            approval = verify_publication_environment()
        package(build, args.output_dir.resolve(), label, args.mode, env, args.runtime_dir,
                args.native, args.verify_package_only, approval)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('CI/deployment failed:', error, file=sys.stderr)
        sys.exit(1)
