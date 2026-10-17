"""Run the current native suite and preserve immutable controller integration evidence.

Requires a completed Release build. No user images or recipes are opened.
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import xml.etree.ElementTree as ET


def main():
    root = Path(__file__).resolve().parents[1]
    os.chdir(root)
    now = datetime.now(timezone.utc)
    prefix = 'integration-cache-memoire'
    previous = sorted(Path('docs/validation/raw').glob(prefix + '-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3]) + 1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-' + now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-latency-investigation') / campaign
    work.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = ';'.join(('C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                                'C:/dev/vcpkg/installed/x64-windows/bin', env['PATH']))
        env['QT_PLUGIN_PATH'] = 'C:/Qt/6.11.2/msvc2022_64/plugins'
    env['QT_QPA_PLATFORM'] = 'offscreen'
    xml = (work / 'ctest.xml').resolve()
    command = ['ctest', '--test-dir', 'build-windows-msvc', '-C', 'Release', '--output-on-failure',
               '--output-junit', str(xml)]
    tested = subprocess.run(command, env=env, capture_output=True, text=True, timeout=600)
    cases = []
    if xml.exists():
        for case in ET.parse(xml).getroot().iter('testcase'):
            cases.append(dict(name=case.get('name'), duration_seconds=float(case.get('time', '0')),
                status=case.get('status'), failure=case.find('failure') is not None,
                stdout=case.findtext('system-out', '')))
    sources = ['src/cache/SystemMemory.cpp', 'src/cache/SystemMemory.h',
        'src/cache/ImageCacheController.cpp', 'src/cache/ImageCacheController.h',
        'src/cache/DecodedImageCache.cpp', 'src/cache/DecodedImageCache.h',
        'src/editor/imageeditor.cpp', 'src/editor/imageeditor.h', 'src/raw/RawDecoder.cpp',
        'src/raw/RawDecoder.h', 'src/qml/Main.qml', 'tests/image_cache_controller_test.cpp',
        'tests/editor_image_cache_test.cpp', 'tests/controlled_memory.h', 'CMakeLists.txt',
        'tests/verify_memory_cache_integration.py', 'build-windows-msvc/Release/appIPR.exe',
        'build-windows-msvc/Release/imageCacheControllerTests.exe',
        'build-windows-msvc/Release/editorImageCacheTests.exe']
    hashes = {}
    for name in sources:
        with Path(name).open('rb') as stream:
            hashes[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    passed = tested.returncode == 0 and len(cases) >= 26 and all(not c['failure'] for c in cases)
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        measured_at_utc=now.isoformat(), group='performance-ouverture',
        status='passed' if passed else 'failed', platform=platform.platform(),
        git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
        dirty_worktree=True, inputs=hashes, command=command, returncode=tested.returncode,
        stdout=tested.stdout, stderr=tested.stderr, tests=cases,
        policy=dict(global_high_percent=80, resume_percent=75, reserve_percent=20,
                    additional_safety_percent=2, minimum_safety_bytes=128*1024**2,
                    retention_percent=25, maximum_retention_bytes=8*1024**3,
                    sample_interval_ms=500, prefetch_delay_ms=250, simultaneous_prefetch=1,
                    peak_bytes_per_pixel=96, peak_margin_bytes=128*1024**2),
        coverage=['Deterministic injected memory: LRU, ownership, pressure/hysteresis, reservations, concurrent admissions, commit',
                  'Editor: preload promotion, no speculative sidecars, recipe restore, latest selection, denied admission keeps displayed image',
                  'Native Windows RAM sampler smoke; full current native test suite'],
        limitations=['No physical 80% hard guarantee, no peak RSS validation',
                     'No new real-corpus timing or pixel-parity campaign',
                     'Linux/macOS implementations not compiled or executed in this campaign',
                     'No GitHub CI or standalone deployment; local Release build'])
    target = Path('docs/validation/raw') / (campaign + '.json')
    with target.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, ensure_ascii=False, allow_nan=False)
        stream.write('\n')
    print(tested.stdout, flush=True)
    print(target, flush=True)
    if not passed:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
