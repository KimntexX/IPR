"""GitHub native runner adapter; keeps dependency arguments out of YAML shells."""
import os
from pathlib import Path
import platform
import subprocess
import sys

args = [sys.executable, 'scripts/ci.py'] + sys.argv[1:]
if os.name == 'nt':
    root = Path(os.environ['IPR_VCPKG_ROOT'])
    args += ['--cmake-arg=-DCMAKE_TOOLCHAIN_FILE=' + str(root / 'scripts/buildsystems/vcpkg.cmake'),
             '--cmake-arg=-DVCPKG_TARGET_TRIPLET=x64-windows',
             '--runtime-dir', 'build-ci/vcpkg_installed/x64-windows/bin']
    # libraw uses manual-link rather than a CMake config package.
    args += ['--cmake-arg=-DLIBRAW_LIBRARY=' + str(Path('build-ci/vcpkg_installed/x64-windows/lib/manual-link/raw.lib').resolve())]
elif platform.system() == 'Darwin':
    prefix = subprocess.check_output(['brew', '--prefix'], text=True).strip()
    args += ['--cmake-arg=-DOpenCV_DIR=' + prefix + '/opt/opencv/lib/cmake/opencv4',
             '--cmake-arg=-DCMAKE_PREFIX_PATH=' + os.environ['QT_ROOT_DIR'] + ';' + prefix]
sys.exit(subprocess.call(args))
