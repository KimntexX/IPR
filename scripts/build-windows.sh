#!/usr/bin/env bash
set -euo pipefail

# Run with: bash scripts/build-windows.sh [--full-tests | --paths FILE ...]
# WSL builds Windows binaries through PowerShell/MSVC, never through Linux CMake.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; assert sys.version_info >= (3, 9)' >/dev/null 2>&1; then
        exec "$candidate" "$script_dir/build_windows.py" "$@"
    fi
done
echo 'Python >= 3.9 is required (python3 in WSL, or Python in Git Bash).' >&2
exit 1
