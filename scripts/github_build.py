"""GitHub native runner adapter; keeps dependency arguments out of YAML shells."""
import os
from pathlib import Path
import platform
import subprocess
import sys

def native_arguments():
    if platform.system() == 'Windows':
        root = Path(os.environ['IPR_VCPKG_ROOT'])
        return ['--cmake-arg=-DCMAKE_TOOLCHAIN_FILE=' + str(root / 'scripts/buildsystems/vcpkg.cmake'),
                '--cmake-arg=-DVCPKG_TARGET_TRIPLET=x64-windows',
                '--runtime-dir', 'build-ci/vcpkg_installed/x64-windows/bin',
                # LibRaw uses manual-link rather than a CMake config package.
                '--cmake-arg=-DLIBRAW_LIBRARY=' + str(Path('build-ci/vcpkg_installed/x64-windows/lib/manual-link/raw.lib').resolve())]
    if platform.system() == 'Darwin':
        prefix = subprocess.check_output(['brew', '--prefix'], text=True).strip()
        qt = os.environ['QT_ROOT_DIR']
        if not prefix or not qt:
            raise ValueError('The Homebrew and selected Qt prefixes must be explicit')
        raw, tiff, opencv = (prefix + '/opt/' + name for name in ('libraw', 'libtiff', 'opencv'))
        # A broad /opt/homebrew/include (or /usr/local/include) exposes another
        # Qt version before framework headers. Scope the two header search
        # roots to their formulae; retain the selected Qt in both build modes.
        return ['--cmake-arg=-DOpenCV_DIR=' + opencv + '/lib/cmake/opencv4',
                '--cmake-arg=-DQt6_DIR=' + qt + '/lib/cmake/Qt6',
                '--cmake-arg=-DLIBRAW_INCLUDE_DIR=' + raw + '/include',
                '--cmake-arg=-DLIBRAW_LIBRARY=' + raw + '/lib/libraw.dylib',
                '--cmake-arg=-DTIFF_INCLUDE_DIR=' + tiff + '/include',
                '--cmake-arg=-DCMAKE_PREFIX_PATH=' + ';'.join((qt, raw, tiff, opencv))]
    return []


def main(arguments=None):
    args = [sys.executable, 'scripts/ci.py'] + (sys.argv[1:] if arguments is None else arguments)
    return subprocess.call(args + native_arguments())


if __name__ == '__main__':
    sys.exit(main())
