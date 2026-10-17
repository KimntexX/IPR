"""Verify production profile compensation against accepted native witnesses.

All 16 R6 must remain pixel-identical on the checked step-eight grid; the
three R7 must match the accepted +0.33 EV study without changing source EV.
No photos, recipes or reference JSONs are modified.
"""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

import numpy as np

from analysis import native

MEMORY = Path('docs/validation/raw/analyse-neutres-rec2020-v001-20261015T212901690091Z.json')
STUDY = Path('docs/validation/raw/investigation-baseline-r7-v002-20261015T214809555208Z.json')
CONFIG = Path('config/raw-normalization.json')
EXE = Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')


def compare_codes(before, after, exact=False):
    if before.shape != after.shape: raise ValueError('Witness geometry changed')
    error = np.abs(before.astype(np.int32)-after.astype(np.int32))
    maximum = int(error.max())
    return dict(max_16bit_codes=maximum, mean_abs_codes=float(error.mean()),
                required_max_codes=0 if exact else 4, passed=maximum <= (0 if exact else 4))


def main():
    import cv2  # Manual image analysis only: numeric unit tests run without OpenCV.
    now = datetime.now(timezone.utc)
    prefix = 'integration-profile-rendering'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-colorimetrie-inventory')/campaign
    work.mkdir(exist_ok=False)
    memory, study = native.read_json(MEMORY), native.read_json(STUDY)
    candidates = {r['scene']: r['variants']['0.33'] for r in study['records']}
    files = [MEMORY, STUDY, CONFIG, EXE, Path(__file__)] + [Path(p) for p in (
        'src/raw/RawNormalization.cpp', 'src/raw/RawNormalization.h',
        'src/raw/CameraCharacterization.cpp', 'src/raw/CameraCharacterization.h', 'src/raw/RawDecoder.cpp',
        'src/raw/CameraSignal.cpp', 'src/raw/CameraSignal.h',
        'src/core/ImageImporter.cpp', 'src/core/ImageImporter.h', 'src/core/ImageMetadata.h',
        'src/profiles/PreparedProfileCache.cpp', 'src/profiles/PreparedProfileCache.h',
        'src/raw/RawDecoder.h', 'src/core/ColorPipeline.h', 'src/profiles/CameraProfileEngine.cpp',
        'src/profiles/CameraProfileEngine.h', 'tests/raw_normalization_corpus_probe.cpp')]
    hashes = {str(p): native.sha256(p) for p in files}
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='base-colorimetrique', measured_at_utc=now.isoformat(), inputs=hashes,
        status='auditing', records=[], method=dict(sample_step=8, optics=False,
            R6='Exact comparison to current neutral exports replay, compensation required zero',
            R7='Compare to accepted 0.33 study, <=4 uint16 codes; baseline .35 and compensation -.02 distinct'),
        units=dict(codes='sRGB uint16', exposure='EV before DCP LUTs/curve'),
        tools=dict(opencv=cv2.__version__, numpy=np.__version__),
        limitations=['Step-eight grid is not every full-resolution pixel',
                     'Native integration, not a fresh manual IHM export or universal R7 validation'])
    active = 'preflight'
    try:
        witnesses = {}
        for a in memory['audits']:
            active = a['scene']
            for p in (a['source'], a['profile']):
                if native.sha256(p) != memory['inputs'][p]: raise ValueError('Changed RAW/DCP source: '+p)
                hashes[p] = memory['inputs'][p]
            if active in candidates:
                witness = Path('build-colorimetrie-inventory')/study['campaign_id']/(active+'-ev-0.33')/'native-current.png'
                digest = candidates[active]['image_sha256']
            else:
                witness = Path('build-colorimetrie-inventory')/memory['campaign_id']/active/'native-current.png'
                digest = a['replay']['native']['native_preview_sha256']
            if native.sha256(witness) != digest: raise ValueError('Changed/missing native witness')
            hashes[str(witness)] = digest
            witnesses[active] = witness
        env = os.environ.copy()
        env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                               'C:/dev/vcpkg/installed/x64-windows/bin', env.get('PATH', '')])
        env['QT_QPA_PLATFORM'] = 'offscreen'
        for a in memory['audits']:
            active = a['scene']
            print('VERIFY', active, flush=True)
            out = work/active
            subprocess.run([str(EXE.resolve()), str(Path(a['source']).resolve()), a['profile'],
                str(CONFIG.resolve()), str(out.resolve()), '--working-space-source'], env=env,
                check=True, stdout=subprocess.DEVNULL, timeout=180)
            proof = native.read_json(out/'manifest.json')
            old = a['replay']['native']
            r7 = active in candidates
            for key in ('white_level', 'source_kelvin', 'baseline_exposure_ev', 'profile_sha256'):
                if proof[key] != old[key]: raise ValueError('Source calibration changed: '+key)
            expected = -.02 if r7 else 0
            if (proof['profile_exposure_compensation_ev'] != expected or not proof['normalization_applied'] or
                proof['CA'] or proof['lens_geometry'] or proof['lens_vignetting']):
                raise ValueError('Unexpected compensation/normalization/optics')
            before = cv2.imread(str(witnesses[active]), cv2.IMREAD_UNCHANGED)
            after = cv2.imread(str(out/'native-current.png'), cv2.IMREAD_UNCHANGED)
            measurement = compare_codes(before, after, exact=not r7)
            result['records'].append(dict(scene=active, camera=a['calibration']['camera_model'],
                iso=a['calibration']['iso'], native=proof, comparison=measurement))
            print('CODES', measurement, flush=True)
            if not measurement['passed']: raise ValueError('Native rendering differs from accepted witness')
        for p, digest in hashes.items():
            if native.sha256(p) != digest: raise ValueError('Input changed during verification: '+p)
        result['status'] = 'complete'
    except Exception as error:
        result.update(status='stopped', failure=dict(input=active, reason=str(error)))
        print('STOP', active, str(error), flush=True)
    result['completed_at_utc'] = datetime.now(timezone.utc).isoformat()
    target = Path('docs/validation/raw')/(campaign+'.json')
    native.write_immutable(target, result)
    print(target, flush=True)
    return 0 if result['status'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
