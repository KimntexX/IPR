"""Read-only diagnosis of the supplied 0081 neutral -2 export and recipe."""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

import cv2
import numpy as np

from analysis import native
from inspect_saved_raw_state import inspect as saved_state
from compare_raw_radiometric_output import distribution
from verify_profile_rendering_integration import compare_codes


def main():
    now = datetime.now(timezone.utc)
    prefix = 'investigation-export-0081'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-colorimetrie-inventory')/campaign
    work.mkdir(exist_ok=False)
    raw = Path('tests/image/base/6V1A0081.CR3')
    state = saved_state(Path(str(raw)+'.ipr'))
    profile = Path(state['raw']['profile'])
    config = Path('config/raw-normalization.json')
    exe = Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')
    folder = raw.parent/'neutre'
    paths = dict(previous=folder/'6V1A0081-IPR_RAW_Audit.tiff',
                 current=folder/'6V1A0081-IPR_RAW_Audit-2.tiff', LR=folder/'6V1A0081.tif')
    sources = [raw, profile, config, exe, Path(state['file']), Path(__file__)] + [Path(p) for p in (
        'tests/analysis/native.py', 'tests/inspect_saved_raw_state.py',
        'tests/raw_normalization_corpus_probe.cpp', 'src/editor/imageeditor.cpp',
        'src/raw/RawDecoder.cpp', 'src/raw/RawNormalization.cpp', 'src/profiles/CameraProfileEngine.cpp')]
    hashes = {str(p): native.sha256(p) for p in sources}
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='base-colorimetrique', measured_at_utc=now.isoformat(), inputs=hashes,
        status='auditing', saved_recipe=state, exports={}, replays={}, comparisons={},
        method=dict(sample_step=8, bits=16, output='sRGB', master='Rec.2020 linear D65',
                    hypothesis='Historical export without calibration; current engine follows updated model configuration automatically'),
        units=dict(codes='unsigned 16bit sRGB', Y='linear sRGB relative luminance', fractions='percent'),
        tools=dict(numpy=np.__version__, opencv=cv2.__version__),
        limitations=['Saved state is current, not independent TIFF-time metadata',
                     'Exact replay checks a step-eight grid, not all full-resolution pixels',
                     'LR distributions are independent geometries, not registered MAE/DeltaE',
                     'No image, sidecar, profile or production configuration modified'])
    try:
        samples = {}
        for role, path in paths.items():
            pixels, meta, sig = native.inspect(path)
            hashes[str(path)] = meta['sha256']
            samples[role] = np.asarray(pixels[::8, ::8, :3]).copy()
            result['exports'][role] = dict(metadata=meta, distribution=distribution(pixels))
            native.verify_unchanged(path, sig, meta['sha256'])
            del pixels
        env = os.environ.copy()
        env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                               'C:/dev/vcpkg/installed/x64-windows/bin', env.get('PATH', '')])
        env['QT_QPA_PLATFORM'] = 'offscreen'
        for name, mode in (('calibrated', '--working-space-source'),
                           ('uncalibrated', '--working-space-uncalibrated')):
            out = work/name
            subprocess.run([str(exe.resolve()), str(raw.resolve()), str(profile.resolve()),
                            str(config.resolve()), str(out.resolve()), mode],
                           env=env, check=True, timeout=180, stdout=subprocess.DEVNULL)
            proof = native.read_json(out/'manifest.json')
            if proof['normalization_applied'] != (name == 'calibrated'):
                raise ValueError('Unexpected actual calibration state')
            if proof['profile_exposure_compensation_ev'] != 0:
                raise ValueError('R7 compensation leaked to 0081')
            png = cv2.imread(str(out/'native-current.png'), cv2.IMREAD_UNCHANGED)[..., :3][..., ::-1]
            result['replays'][name] = dict(native=proof, proof_sha256=native.sha256(out/'manifest.json'))
            result['comparisons'][name] = {role: compare_codes(samples[role], png, exact=True)
                                          for role in ('previous', 'current')}
        result['manifest_hash_matches_saved'] = state['raw']['manifestSha256'] == hashes[str(config)]
        result['diagnosis_confirmed'] = (
            not result['manifest_hash_matches_saved'] and state['raw']['normalizationEnabled'] and
            result['comparisons']['calibrated']['previous']['passed'] and
            result['comparisons']['uncalibrated']['current']['passed'] and
            not result['comparisons']['calibrated']['current']['passed'])
        for p, digest in hashes.items():
            if native.sha256(p) != digest: raise ValueError('Input changed: '+p)
        result['status'] = 'complete'
    except Exception as error:
        result.update(status='stopped', failure=str(error))
    result['completed_at_utc'] = datetime.now(timezone.utc).isoformat()
    target = Path('docs/validation/raw')/(campaign+'.json')
    native.write_immutable(target, result)
    print(target, result['status'], result.get('diagnosis_confirmed'), result.get('comparisons'), flush=True)
    return 0 if result['status'] == 'complete' else 1


if __name__ == '__main__':
    raise SystemExit(main())
