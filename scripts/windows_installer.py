"""Compile a per-user Windows installer from an already verified public standalone."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LABEL = re.compile(r'(0\.1\.\d+-alpha|0\.(?:[2-9]|[1-9]\d+)\.\d+-beta|1\.\d+\.\d+)(?:\+[A-Za-z0-9.-]+)?\Z')
EXCLUDED = {'bin/config/ipr-ui.ini', 'package-manifest.json'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def installed_manifest(stage, label):
    """Validate provenance and every file, then inventory the installed variant."""
    if not LABEL.fullmatch(label):
        raise ValueError('Invalid release label')
    manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
    if manifest.get('mode') != 'standalone' or manifest.get('version') != label or manifest.get('private_profiles') is not False:
        raise ValueError('Installer requires a matching public standalone manifest')
    if manifest.get('smoke', {}).get('startup') != 'passed':
        raise ValueError('Standalone smoke test must have succeeded')
    actual = {}
    for path in stage.rglob('*'):
        if path.is_symlink():
            raise ValueError('Symlinks are forbidden in installer payload')
        if path.is_file() and path.name != 'package-manifest.json':
            actual[path.relative_to(stage).as_posix()] = path
    recorded = {}
    for item in manifest.get('files', []):
        relative = item['path']
        if relative in recorded or PurePosixPath(relative).is_absolute() or '..' in PurePosixPath(relative).parts:
            raise ValueError('Invalid/duplicate payload path')
        recorded[relative] = item
    if set(recorded) != set(actual):
        raise ValueError('Payload inventory mismatch')
    for name, path in actual.items():
        if digest(path) != recorded[name]['sha256'] or path.stat().st_size != recorded[name]['bytes']:
            raise ValueError('Payload modified after smoke/inventory: ' + name)
        if path.suffix.lower() in ('.dcp', '.lcp', '.cr2', '.cr3', '.ipr'):
            raise ValueError('Private profile/image/sidecar in installer')
    required = ('bin/appIPR.exe', 'bin/config/ipr-ui.ini', 'bin/profiles/index.json')
    if any(name not in actual for name in required):
        raise ValueError('Incomplete Windows standalone')
    if not any(name in actual for name in ('plugins/platforms/qwindows.dll',
                                           'bin/platforms/qwindows.dll', 'bin/plugins/platforms/qwindows.dll')):
        raise ValueError('Windows Qt platform plugin missing')
    if json.loads((stage / 'bin/profiles/index.json').read_text(encoding='utf8')).get('entries') != []:
        raise ValueError('Public profile index must be empty')
    manifest['distribution'] = 'windows-installer-per-user'
    manifest['source_portable_manifest_sha256'] = digest(stage / 'package-manifest.json')
    manifest['files'] = [item for item in manifest['files'] if item['path'] not in EXCLUDED]
    manifest['preferences'] = 'Qt AppConfigLocation/ipr-ui.ini; user data is not installer-owned'
    manifest['signed_release'] = False
    return manifest


def find_compiler(explicit=None):
    candidates = [explicit or os.environ.get('IPR_ISCC'), shutil.which('ISCC.exe')]
    for variable in ('ProgramFiles(x86)', 'ProgramFiles'):
        if os.environ.get(variable):
            candidates.append(str(Path(os.environ[variable]) / 'Inno Setup 6/ISCC.exe'))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate).resolve()
    raise FileNotFoundError('Inno Setup 6.3+ compiler missing; set IPR_ISCC or pass --iscc')


def exercise_installer(binary, work):
    """Disposable GitHub-hosted Windows runner only, never the developer's PC."""
    if os.name != 'nt' or os.environ.get('GITHUB_ACTIONS') != 'true' or os.environ.get('RUNNER_ENVIRONMENT') != 'github-hosted':
        raise RuntimeError('Installation smoke test requires a disposable GitHub-hosted Windows runner')
    import winreg
    key = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\org.ipr.ImageProcessingRoom.Windows.x64_is1'
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key):
            pass
    except FileNotFoundError:
        pass
    else:
        raise RuntimeError('Refusing to replace an existing IPR installation during smoke test')
    destination = work / 'installed'
    uninstaller = destination / 'unins000.exe'
    try:
        for attempt in (1, 2):
            subprocess.run([str(binary), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/NOICONS',
                            '/DIR=' + str(destination), '/LOG=' + str(work / f'install-{attempt}.log')],
                           check=True, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
            if (destination / 'bin/config/ipr-ui.ini').exists():
                raise RuntimeError('Installed application must not use the portable preferences file')
            if attempt == 1:
                # Not owned by the installer: models personal files without touching real AppData.
                (destination / 'personal-profile.keep').write_bytes(b'preserve this unowned file')
        if (destination / 'personal-profile.keep').read_bytes() != b'preserve this unowned file':
            raise RuntimeError('Upgrade changed an unowned file')
        from ci import smoke, runtime_environment
        smoke(destination, 'standalone', runtime_environment())
    finally:
        if uninstaller.is_file():
            subprocess.run([str(uninstaller), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/LOG=' + str(work / 'uninstall.log')],
                           check=True, timeout=120, creationflags=subprocess.CREATE_NO_WINDOW)
    if (destination / 'bin/appIPR.exe').exists():
        raise RuntimeError('Uninstaller left the installed executable behind')
    if (destination / 'personal-profile.keep').read_bytes() != b'preserve this unowned file':
        raise RuntimeError('Uninstaller deleted an unowned file')
    print('Installer install/reinstall/startup/uninstall smoke passed', flush=True)


def build_installer(stage, output, label, iscc=None, verify_only=False, exercise_install=False):
    if os.environ.get('GITHUB_ACTIONS') == 'true' and not verify_only:
        from delivery_policy import verify_publication_environment
        verify_publication_environment()
    stage, output = Path(stage).resolve(), Path(output).resolve()
    manifest = installed_manifest(stage, label)
    compiler = find_compiler(iscc)
    name = 'IPR-' + label + '-windows-x64-setup'
    artifact = output / (name + '.exe')
    if artifact.exists() or artifact.with_suffix('.exe.sha256').exists():
        raise FileExistsError('Installer output already exists')
    output.mkdir(parents=True, exist_ok=True)
    # Temporary compilation for validation never publishes a downloadable EXE.
    with tempfile.TemporaryDirectory(prefix='ipr-installer-') as temporary:
        work = Path(temporary)
        metadata = work / 'package-manifest.json'
        metadata.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf8')
        command = [str(compiler), '/Qp', '/O' + str(work), '/DPayloadDir=' + str(stage),
                   '/DInstallerManifest=' + str(metadata), '/DReleaseLabel=' + label,
                   '/DNumericVersion=' + label.split('-')[0].split('+')[0], '/DArtifactName=' + name,
                   str(ROOT / 'packaging/windows/ipr.iss')]
        subprocess.run(command, check=True, timeout=600)
        built = work / artifact.name
        if not built.is_file():
            raise RuntimeError('Compiler did not produce the expected installer')
        if exercise_install:
            try:
                exercise_installer(built, work)
            finally:
                logs = ROOT / 'ci-logs/windows-installer'
                logs.mkdir(parents=True, exist_ok=True)
                for log in work.glob('*.log'):
                    shutil.copy2(log, logs / log.name)
        if not verify_only:
            # Exclusive creation protects existing outputs even after a concurrent build.
            with artifact.open('xb') as destination, built.open('rb') as source:
                shutil.copyfileobj(source, destination)
            artifact.with_suffix('.exe.sha256').write_text(digest(artifact) + '  ' + artifact.name + '\n', encoding='ascii')
    return None if verify_only else artifact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'dist')
    parser.add_argument('--version', required=True)
    parser.add_argument('--iscc', type=Path)
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Installer compilation requires Windows')
    print(build_installer(args.stage, args.output_dir, args.version, args.iscc, args.verify_only))


if __name__ == '__main__':
    main()
