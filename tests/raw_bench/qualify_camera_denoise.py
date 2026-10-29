"""Record immutable N2 synthetic/module integration evidence, without image exports."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--module-tool', type=Path, required=True)
    parser.add_argument('--profile-tool', type=Path)
    parser.add_argument('--module-only', action='store_true',
                        help='Qualify module and bench suites only; engine integration is explicitly pending.')
    parser.add_argument('--ctest', type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', required=True)
    args = parser.parse_args()
    if args.module_only and args.profile_tool:
        parser.error('--module-only must not use a stale --profile-tool')
    if not args.module_only and args.profile_tool is None:
        parser.error('--profile-tool required unless --module-only is explicit')
    root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p.resolve()) for p in args.runtime_dir) + os.pathsep + env.get('PATH', '')
    sources = ('src/raw/CameraDenoise.h', 'src/raw/CameraDenoise.cpp',
               'tests/raw_bench/test_camera_denoise.cpp', 'tests/raw_bench/CMakeLists.txt',
               'tests/raw_bench/qualify_camera_denoise.py', 'tests/camera_profile_engine_test.cpp')
    data = dict(schema_version=1, created_utc=stamp, study='experimental_camera_denoise_N2',
                revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                sources={name: dict(sha256=sha256(root/name), text=(root/name).read_text(encoding='utf-8')) for name in sources},
                tools={str(p.resolve()): sha256(p.resolve()) for p in
                       (args.module_tool, args.profile_tool) if p is not None},
                engine_integration='pending' if args.module_only else 'tested_below',
                commands=[], limits=[
                    'Synthetic and profile integration only; real-scene N3 remains open.',
                    'Noise is empirical post-RCD, not a calibrated CFA a*signal+b model.',
                    'Benchmarks exclude RAW decode, profile, editor caches and UI.',
                    'Peak bytes describe the synthetic process, not whole-system RAM.',
                    'This evidence does not prove that N1 supplies signed pre-LCP input.',
                    'No Lightroom equivalence or visual validation is claimed.'])
    module = str(args.module_tool.resolve())
    commands = [[module], [module, '--benchmark', '2048', '1365'],
                [module, '--benchmark', '6000', '4000']]
    if not args.module_only:
        commands.append([str(args.profile_tool.resolve())])
    commands.append([str(args.ctest.resolve()), '--test-dir', str(args.module_tool.resolve().parents[1]),
                     '-C', 'Release', '--output-on-failure'])
    failure = False
    for command in commands:
        try:
            result = subprocess.run(command, cwd=root, env=env, text=True, encoding='utf-8',
                                    errors='replace', capture_output=True, timeout=180)
            record = dict(argv=command, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)
            failure = failure or result.returncode != 0
        except subprocess.TimeoutExpired as exc:
            record = dict(argv=command, error='timeout', timeout_seconds=exc.timeout)
            failure = True
        data['commands'].append(record)
        print(json.dumps(record, ensure_ascii=False), flush=True)
        if failure:
            break
    data['verdict'] = ('fail' if failure else 'pass_synthetic_module_engine_integration_pending'
                       if args.module_only else 'pass_synthetic_module_and_profile_integration_not_N3')
    output = root/'docs/validation/raw'/f'camera-denoise-n2-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output)
    return 1 if failure else 0


if __name__ == '__main__':
    sys.exit(main())
