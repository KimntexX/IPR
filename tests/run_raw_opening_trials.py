"""Offline DCP curve/baseline experiments; immutable memory, no production edits."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import struct
import subprocess

from analysis import native
from investigate_tonal_base import read_dcp


def without_curve_bytes(data):
    """Hide only ProfileToneCurve in a disposable DCP; preserve all other bytes."""
    result = bytearray(data)
    endian = {'II': '<', 'MM': '>'}.get(data[:2].decode('ascii'))
    if endian is None or struct.unpack_from(endian+'H', data, 2)[0] != 0x4352:
        raise ValueError('Not a DCP')
    offset = struct.unpack_from(endian+'I', data, 4)[0]
    count = struct.unpack_from(endian+'H', data, offset)[0]
    found = 0
    for i in range(count):
        position = offset+2+12*i
        tag = struct.unpack_from(endian+'H', data, position)[0]
        if tag == 65000:
            raise ValueError('Reserved diagnostic tag already present')
        if tag == 50940:
            struct.pack_into(endian+'H', result, position, 65000)
            found += 1
    if found != 1:
        raise ValueError('Expected one ProfileToneCurve')
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('summary', type=Path)
    parser.add_argument('--executable', type=Path, default=Path('build-windows-msvc/Release/rawOpeningTrials.exe'))
    args = parser.parse_args()
    now = datetime.now(timezone.utc)
    campaign = 'essais-ouverture-dcp-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-colorimetrie-inventory')/campaign
    work.mkdir(parents=True, exist_ok=False)
    memory = native.read_json(args.summary)
    if memory['missing_LR'] or len(memory['comparisons']) != memory['RAW_count']:
        raise ValueError('Incomplete reference corpus')
    inputs = [args.summary, args.executable, Path(__file__), Path('tests/raw_opening_trials.cpp'), Path('tests/raw_opening_trial_helpers.h'),
              Path('src/raw/RawDecoder.cpp'), Path('src/raw/RawDecoder.h'),
              Path('src/profiles/CameraProfileEngine.cpp'), Path('src/profiles/CameraProfileEngine.h'),
              Path('src/core/ColorPipeline.cpp'), Path('src/core/ColorPipeline.h')]
    probes = {}
    for entry in memory['inputs']:
        path = Path(entry['file'])
        if native.sha256(path) != entry['sha256']:
            raise ValueError('Previous evidence changed: '+str(path))
        if path.name.startswith('inspection-ouverture-scene-'):
            record = native.read_json(path)['record']
            probes[record['scene']] = record
    baseline_path = Path('docs/validation/raw/audit-dng-reference-20261008.json')
    inputs.append(baseline_path)
    baseline = {Path(row['path']).stem: row['ifd0']['baseline_exposure']
                for row in native.read_json(baseline_path)['scenes']}
    profiles, scenes = {}, []
    for row in memory['comparisons']:
        probe = probes[row['scene']]
        source, profile = Path(probe['source']), Path(probe['profile_path'])
        for path, expected in ((source, probe['sha256']), (profile, probe['profile_sha256']),
                               (Path(row['LR_file']), row['LR_sha256'])):
            if native.sha256(path) != expected:
                raise ValueError('Input no longer matches memory: '+str(path))
            inputs.append(path)
        if profile not in profiles:
            tags = read_dcp(profile)
            if tags.get(52551, (0,))[0] != 0:
                raise ValueError('SDR experiment only')
            target = work/(str(len(profiles))+'-without-tone-curve.dcp')
            with target.open('xb') as output:
                output.write(without_curve_bytes(profile.read_bytes()))
            changed = read_dcp(target)
            if 50940 in changed or any(changed.get(k) != v for k,v in tags.items() if k != 50940):
                raise ValueError('Unexpected diagnostic DCP modification')
            profiles[profile] = dict(without_curve=str(target.resolve()), curve=tags[50940])
            inputs.append(target)
        scene = dict(scene=row['scene'], RAW=str(source.resolve()), DCP_sha256=probe['profile_sha256'],
                     **profiles[profile])
        if row['scene'] in baseline:
            scene['camera_baseline_EV'] = baseline[row['scene']]
            scene['camera_baseline_provenance'] = 'Historical Adobe DNG audit, same scene name; not physical calibration'
        scenes.append(scene)
    spec = work/'specification.json'
    native.write_immutable(spec, dict(scenes=scenes, spline='Natural cubic, zero endpoint second derivative; not asserted to match Adobe'))
    inputs.append(spec)
    hashes = {str(p): native.sha256(p) for p in inputs}
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin',
                               'C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    result_path = work/'native-result.json'
    subprocess.run([str(args.executable.resolve()),str(spec.resolve()),str(result_path.resolve())],env=env,check=True)
    result = native.read_json(result_path)
    if len(result['records']) != len(scenes):
        raise ValueError('Incomplete native experiment')
    references = {row['scene']:row for row in memory['comparisons']}
    for record in result['records']:
        reference = references[record['scene']]
        record['LR_reference'] = {k:reference[k] for k in ('LR_file','LR_sha256','LR_WB','LR_T','LR_tint','LR_Y')}
        for values in record['models'].values():
            values['relative_mean_Y_to_LR_percent'] = 100*(values['mean_Y']/reference['LR_Y']-1)
    for path, expected in hashes.items():
        if native.sha256(Path(path)) != expected:
            raise ValueError('Input changed during experiment: '+path)
    result.update(campaign_id=campaign, schema_version=1, data_version=1, group='base-colorimetrique',
                  inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                  protocol='docs/validation/protocols/protocole-raw-profils.md',
                  protocol_sha256=native.sha256(Path('docs/validation/protocols/protocole-raw-profils.md')),
                  units=dict(Y='relative linear sRGB luminance',percent='percent or percentage point differences as labelled'),
                  method=dict(spline='Natural cubic with zero second derivatives at endpoints',
                              interpolation_witness='Linear replay must match production within 2e-5 max RGB error',
                              baseline='Only six scenes with historical Adobe DNG evidence, no R7 extrapolation',
                              production_modified=False,normalization=False,optics=False),
                  limitations=['No LR pixel alignment or inter-engine MAE',
                               'Mean Y is not physical fidelity or perceived brightness',
                               'ROI fixed geometry, flower-specific only for 0081',
                               'Cubic boundary conditions are experimental, not certified Adobe renderer',
                               'Baseline experiment does not alter white/black levels or camera calibration',
                               'Spline may overshoot; no undocumented output clamp before normal sRGB output'])
    destination=Path('docs/validation/raw')/(campaign+'.json')
    native.write_immutable(destination,result)
    print(destination,flush=True)


if __name__ == '__main__':
    main()
