"""Replay seven neutral RAWs with the current engine against a saved native witness.

No photograph/sidecar mutation. Outputs are diagnostic PNGs and immutable
analysis JSON, not generator exports. A failing threshold is recorded as such.
"""
import argparse
from datetime import datetime, timezone
import os
import platform
from pathlib import Path
import subprocess
import numpy as np
from analysis import native
from qualify_headroom_candidates import compare_candidate
from analyse_base_colorimetrique_v2 import srgb_lab, delta_e_2000


def comparison(before, after):
    result = compare_candidate(before, after)
    delta = np.abs(before.astype(np.int32) - after.astype(np.int32))
    de = delta_e_2000(srgb_lab(before.astype(float)/65535)[0],
                      srgb_lab(after.astype(float)/65535)[0])
    result.update(max_16bit_codes=int(delta.max()),
                  delta_E00_p99=float(np.percentile(de, 99, method='lower')),
                  neutral_gate_pass=bool(delta.max() <= 4 and de.mean() <= .01 and
                                         np.percentile(de, 99, method='lower') <= .05))
    return result


def main():
    import cv2
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('--executable', type=Path, default=Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe'))
    args = parser.parse_args()
    memory = native.read_json(args.baseline)
    baseline_root = Path('build-colorimetrie-inventory') / memory['campaign_id']
    manifest = Path('config/raw-normalization.json')
    source_paths = [args.baseline, args.executable, manifest, Path(__file__)] + [Path(p) for p in (
        'src/core/WorkingColorSpace.h', 'src/core/ColorPipeline.cpp', 'src/core/ColorPipeline.h',
        'src/profiles/CameraProfileEngine.cpp', 'src/raw/RawDecoder.cpp',
        'src/raw/RawNormalization.cpp', 'tests/raw_normalization_corpus_probe.cpp',
        'tests/raw_opening_trial_helpers.h', 'tests/dcp_headroom_experiment.h')]
    fingerprints = {str(p): native.sha256(p) for p in source_paths}
    for record in memory['records']:
        proof = record['native']
        for key in ('raw', 'profile'):
            if native.sha256(Path(proof[key])) != proof[key+'_sha256']:
                raise ValueError('Changed source: '+proof[key])
            fingerprints[proof[key]] = proof[key+'_sha256']
        witness = baseline_root / record['scene'] / 'native-current.png'
        if native.sha256(witness) != proof['native_preview_sha256']:
            raise ValueError('Changed/missing witness: '+str(witness))
        fingerprints[str(witness)] = proof['native_preview_sha256']
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
    prefix = 'integration-rec2020-neutres'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-{stamp}'
    root = Path('build-colorimetrie-inventory') / campaign
    root.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                            'C:/dev/vcpkg/installed/x64-windows/bin', env.get('PATH','')])
    env['QT_QPA_PLATFORM'] = 'offscreen'
    env['QT_PLUGIN_PATH'] = 'C:/Qt/6.11.2/msvc2022_64/plugins'
    records = []
    for record in memory['records']:
        scene, old = record['scene'], record['native']
        print('Rec.2020 replay:', scene, flush=True)
        out = root / scene
        subprocess.run([str(args.executable.resolve()), old['raw'], old['profile'],
                        str(manifest.resolve()), str(out.resolve()), '--working-space-source'],
                       env=env, check=True, stdout=subprocess.DEVNULL, timeout=180)
        proof = native.read_json(out/'manifest.json')
        if proof.get('master_domain') != 'Rec.2020 linear D65 float32':
            raise ValueError('Stale executable or incorrect master domain')
        for key in ('raw_sha256','profile_sha256','white_level','baseline_exposure_ev','source_kelvin'):
            if proof[key] != old[key]: raise ValueError('Recipe changed: '+key)
        if not proof['no_lcp'] or proof['CA'] or proof['lens_geometry'] or proof['lens_vignetting']:
            raise ValueError('Optical correction changed')
        before = cv2.imread(str(baseline_root/scene/'native-current.png'), cv2.IMREAD_UNCHANGED)[...,:3][...,::-1]
        after = cv2.imread(str(out/'native-current.png'), cv2.IMREAD_UNCHANGED)[...,:3][...,::-1]
        result = comparison(before, after)
        records.append(dict(scene=scene, native=proof, comparison=result))
        print('  max codes:', result['max_16bit_codes'], 'gate:', result['neutral_gate_pass'], flush=True)
    for path, digest in fingerprints.items():
        if native.sha256(Path(path)) != digest: raise ValueError('Source changed during analysis: '+path)
    proof = dict(campaign_id=campaign, schema_version=1, data_version=version,
                 measured_at_utc=datetime.now(timezone.utc).isoformat(),
                 tools=dict(python=platform.python_version(), numpy=np.__version__,
                            opencv=cv2.__version__, libraw=records[0]['native']['backend_version']),
                 units=dict(delta_E00='CIEDE2000 D65', codes='unsigned sRGB 16-bit codes',
                            signed_Y_change='linear sRGB absolute difference'),
                 baseline_campaign_id=memory['campaign_id'], group='base-colorimetrique',
                 source_sha256=fingerprints, records=records,
                 all_neutral_gates_pass=all(r['comparison']['neutral_gate_pass'] for r in records),
                 limitations=['Native grid step 8, not full-resolution detail validation',
                              'One decode per scene, not a performance benchmark',
                              'Current production engine only; historical witness is not a legacy renderer'])
    target = Path('docs/validation/raw') / (campaign+'.json')
    native.write_immutable(target, proof)
    print(target, flush=True)
    return 0 if proof['all_neutral_gates_pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
