"""Manual, serial performance investigation. Never edits original image recipes.

Build openingLatencyProbe first, then run this script from the repository root.
Private RAW copies and their .ipr files are retained under build-latency-investigation.
Only measurements are written to docs/validation/raw (new immutable campaign).
"""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def timing_records(stderr):
    return [dict(file=f, stage=s, elapsed_ms=int(e), delta_ms=int(d))
            for f, s, e, d in re.findall(
                r'IPR RAW timing \| file=(.*?) \| stage=(.*?) \| elapsed_ms=(\d+) \| delta_ms=(\d+)', stderr)]


def main():
    os.chdir(ROOT)
    now = datetime.now(timezone.utc)
    previous = sorted(Path('docs/validation/raw').glob('latence-ouverture-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3]) + 1 if previous else 1
    campaign = f'latence-ouverture-v{version:03d}-' + now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-latency-investigation') / campaign
    work.mkdir(parents=True, exist_ok=False)
    memory = json.loads(Path('docs/validation/raw/analyse-neutres-rec2020-v001-20261015T212901690091Z.json').read_text())
    scenes = {a['scene']: a for a in memory['audits']}
    chosen = [scenes[n] for n in ('6V1A0081', '6V1A0043', '7Y6A7599')]
    exe = Path('build-windows-msvc/Release/openingLatencyProbe.exe').resolve()
    config = Path('config/raw-normalization.json').resolve()
    files = [exe, config, Path(__file__), Path('tests/opening_latency_probe.cpp'),
             *[Path(p) for p in ('src/raw/RawDecoder.cpp', 'src/editor/imageeditor.cpp',
                'src/cache/DecodedImageCache.cpp', 'src/cache/DecodedImageCache.h',
                'src/cache/ImageCacheController.cpp', 'src/cache/SystemMemory.cpp',
                'src/core/ImageImporter.cpp', 'src/profiles/CameraProfileEngine.cpp',
                'src/profiles/PreparedProfileCache.cpp', 'src/core/ColorPipeline.cpp')]]
    for a in chosen:
        files.extend([Path(a['source']), Path(a['profile']), Path(a['source'] + '.ipr')])
    hashes = {str(p): sha(p) for p in files if p.is_file()}
    env = os.environ.copy()
    env['PATH'] = ';'.join(('C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                            'C:/dev/vcpkg/installed/x64-windows/bin', env['PATH']))
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['QT_PLUGIN_PATH'] = 'C:/Qt/6.11.2/msvc2022_64/plugins'
    env['IPR_LOG_RAW_TIMING'] = '1'
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='performance-ouverture', measured_at_utc=now.isoformat(), status='running',
        tools=dict(python=platform.python_version(), platform=platform.platform(),
                   git_head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip(),
                   dirty_worktree=True, configuration='Release'),
        inputs=hashes, scenes=[dict(scene=a['scene'], source=a['source'], dcp=a['profile']) for a in chosen],
        settings=dict(automatic_wb=False, normalization=True, lens_correction=False,
                      chromatic_aberration=False, profile='Camera Neutral',
                      preview_long_side=2048, original_preview=True),
        protocol=dict(stage_runs_per_scene=3, navigation='A B A C A B A',
                      navigation_modes=[dict(budget='0', idle_ms=0), dict(budget='auto', idle_ms=0),
                                        dict(budget='auto', idle_ms=20000)],
                      private_copies=str(work)),
        units=dict(duration='ms', memory='bytes'), records=[],
        limitations=['First process run is NOT OS/disk cold; no filesystem-cache flush',
            'Provider publication/CPU ready, not first QML/GPU frame or physical display',
            'No folder scan, thumbnails, concurrent generator or export in this controlled probe',
            'Three scenes only; no statistical confidence interval',
            'Budget 0 disables retention and prefetch; auto is min(25% RAM, 8 GiB) on this machine only',
            'Global usage is sampled every 50 ms; short peaks between samples are not observed',
            'Cache accounting is not process RSS; other running applications are uncontrolled',
            'Process peaks are Windows working set/commit counters, cumulative per process'])
    def run(label, args):
        print(label, flush=True)
        process = subprocess.run([str(exe), *map(str, args)], env=env, text=True,
                                 capture_output=True, timeout=1200)
        record = dict(label=label, command=[str(exe), *map(str, args)],
                      returncode=process.returncode, stderr=process.stderr,
                      raw_timings=timing_records(process.stderr))
        try:
            record['measurement'] = json.loads(process.stdout)
        except json.JSONDecodeError:
            record['stdout'] = process.stdout
        result['records'].append(record)
        if process.returncode or 'error' in record.get('measurement', {}):
            raise RuntimeError(f'Probe failed: {label}: {process.stderr[-1500:]}')
    try:
        for a in chosen:
            run('stages-' + a['scene'], ['stages', a['source'], a['profile'], config])
        pairs = []
        for a in chosen:
            private = (work / Path(a['source']).name).resolve()
            shutil.copyfile(a['source'], private)
            if sha(private) != sha(a['source']):
                raise RuntimeError('Private copy mismatch')
            pairs.extend([private, a['profile']])
        for mode in result['protocol']['navigation_modes']:
            run(f"navigation-{mode['budget']}-idle{mode['idle_ms']}",
                ['navigation', config, mode['budget'], mode['idle_ms'], *pairs])
        result['original_inputs_unchanged'] = all(sha(p) == h for p, h in hashes.items())
        if not result['original_inputs_unchanged']:
            raise RuntimeError('An input changed during measurement')
        result['status'] = 'complete'
    except Exception as exc:
        result['status'] = 'failed'
        result['error'] = str(exc)
        raise
    finally:
        target = Path('docs/validation/raw') / (campaign + '.json')
        with target.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2, ensure_ascii=False, allow_nan=False)
            stream.write('\n')
        print(target, flush=True)


if __name__ == '__main__':
    main()
