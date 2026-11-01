"""Run camera-axis/profile leakage witnesses; canonical JSON, no TIFF outputs."""
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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool', type=Path, required=True)
    p.add_argument('--base', type=Path, required=True)
    p.add_argument('--label', required=True)
    p.add_argument('--profile-root', type=Path, required=True)
    p.add_argument('--runtime-dir', type=Path, action='append', required=True)
    a = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(p.resolve()) for p in a.runtime_dir) + os.pathsep + env.get('PATH', '')
    paths = ['src/raw/CameraDenoise.h', 'src/raw/CameraDenoise.cpp',
             'tests/raw_bench/camera_denoise_leakage.cpp', 'tests/raw_bench/run_denoise_leakage.py',
             'tests/raw_bench/production/CMakeLists.txt']
    proof = dict(schema_version=1, created_utc=stamp, label=a.label,
                 tool_sha256=sha256(a.tool),
                 revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
                 sources={name: dict(sha256=sha256(root/name), text=(root/name).read_text(encoding='utf-8')) for name in paths},
                 scenes=[], limits=[
                     'Camera patches, not the full-frame filter estimates or Claude original ROIs.',
                     'Three fixed diagonal 768px patches, central 420px measured; optics off.',
                     'No clean truth: highpass/lowpass statistics include real structure.',
                     'Projection is a finite-difference study, not a production profile API.',
                     'No claim that preserving linear Y preserves clipped sRGB luma.',
                     'No profile bounds or GUI changes.'])
    failed = False
    for name in ('7Y6A7599.CR3', 'IMG_7958.CR2', '6V1A9539.CR3'):
        command = [str(a.tool.resolve()), '--input', str((a.base/name).resolve()),
                   '--manifest', str(root/'config/raw-normalization.json'),
                   '--profile-root', str(a.profile_root.resolve()), '--label', a.label]
        try:
            result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=180)
            record = dict(argv=command, returncode=result.returncode, stderr=result.stderr)
            if result.returncode == 0:
                record['measurements'] = json.loads(result.stdout)
            else:
                record['stdout'] = result.stdout
                failed = True
        except subprocess.TimeoutExpired:
            record = dict(argv=command, error='timeout', timeout_seconds=180)
            failed = True
        proof['scenes'].append(record)
        print(name, 'failed' if failed else 'measured', flush=True)
        if failed:
            break
    proof['verdict'] = 'fail' if failed else 'measured_not_quality_validation'
    output = root/'docs/validation/raw'/f'camera-denoise-leakage-v001-{stamp}.json'
    with output.open('x', encoding='utf-8') as handle:
        json.dump(proof, handle, ensure_ascii=False, indent=2, allow_nan=False)
    print(output, proof['verdict'], flush=True)
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
