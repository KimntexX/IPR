"""Audit all neutral exports, replay the current RAW engine, then compare LR/IPR.

Stops at the first incompatible input. Historical JSON and photographs are
read-only. Independent geometries: distributions, never unregistered pixel MAE.
"""
from datetime import datetime, timezone
import os
from pathlib import Path
import platform
import subprocess
import numpy as np
import PIL
from analysis import native
from inspect_saved_raw_state import inspect as sidecar
from compare_raw_radiometric_output import distribution
from analyse_base_colorimetrique_v2 import srgb_lab

BASE = Path('tests/image/base')
TRIAL = Path('docs/validation/raw/essais-radiometriques-ouverture-v001-20261014T145324616293Z.json')
MANIFEST = Path('config/raw-normalization.json')
EXE = Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')
ZERO = ('brightness', 'contrast', 'blur', 'grayscale', 'shadows', 'blacks',
        'highlights', 'whites', 'tint', 'vibrance', 'saturation', 'texture',
        'clarity', 'dehaze', 'grain', 'denoiseLuminance', 'denoiseChrominance',
        'sharpeningAmount', 'automaticWhiteBalance')
LR_ZERO = ('Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012',
           'Whites2012', 'Blacks2012', 'Clarity2012', 'Texture', 'Dehaze',
           'Vibrance', 'Saturation', 'Sharpness', 'LuminanceSmoothing',
           'ColorNoiseReduction', 'LensProfileEnable', 'AutoLateralCA')


def audit_saved(state, manifest_hash):
    s, r = state['settings'], state['raw']
    wrong = {k: s[k] for k in ZERO if s[k] != 0}
    if any(any(v != 0 for v in band) for band in s['mixer']): wrong['mixer'] = s['mixer']
    if any(band[1] != 0 for band in s['grading']): wrong['grading'] = s['grading']
    if any(c and c != [[0., 0.], [1., 1.]] for c in s['curves']): wrong['curves'] = s['curves']
    if wrong: raise ValueError('Non-neutral saved settings: ' + str(wrong))
    if (not state['source_signature_matches'] or not r['normalizationEnabled'] or
            r['lensCorrectionEnabled'] or r['chromaticAberrationEnabled'] or
            not r['engineVersion'].startswith('ipr-render-rec2020-linear-d65-v1|') or
            r['manifestSha256'] != manifest_hash):
        raise ValueError('Saved RAW recipe/source signature is incompatible')


def audit_lr(meta):
    xmp = meta['xmp_settings']
    if xmp.get('CameraProfile') != 'Camera Neutral' or xmp.get('WhiteBalance') != 'As Shot':
        raise ValueError('Expected Camera Neutral / As Shot, got ' +
                         str((xmp.get('CameraProfile'), xmp.get('WhiteBalance'))))
    keys = set(LR_ZERO) | {k for k in xmp if k.startswith(
        ('HueAdjustment', 'SaturationAdjustment', 'LuminanceAdjustment'))}
    wrong = {k: xmp[k] for k in keys if k in xmp and float(xmp[k]) != 0}
    if wrong: raise ValueError('Non-neutral Lightroom settings: ' + str(wrong))


def stats(pixels):
    result = distribution(pixels)
    rgb = native.encoded(pixels, step=8)
    linear = native.linearize(rgb)
    lab, _ = srgb_lab(rgb)
    Y = linear @ np.array([.2126, .7152, .0722])
    result.update(mean_RGB_linear=linear.mean(axis=(0, 1)).tolist(),
                  mean_Lab_D65=lab.mean(axis=(0, 1)).tolist(),
                  mean_chroma_Lab=float(np.hypot(lab[..., 1], lab[..., 2]).mean()),
                  Y_p01_p10_p25_p50_p75_p90_p99=np.percentile(
                      Y, [1, 10, 25, 50, 75, 90, 99], method='lower').tolist(),
                  channel_high_percent=(100*np.mean(rgb >= 65534/65535., axis=(0, 1))).tolist())
    # Independently defined masks: not pixel correspondence or semantic ROIs.
    regions = {'dark_Y_below_point05': Y < .05,
               'mid_Y_point05_to_point5': (Y >= .05) & (Y < .5),
               'bright_Y_at_least_point5': Y >= .5,
               'brightest_five_percent': Y >= np.percentile(Y, 95, method='lower')}
    result['tone_distributions'] = {name: dict(samples=int(mask.sum()),
        fraction_percent=float(100*mask.mean()), mean_Y=float(Y[mask].mean()) if mask.any() else None)
        for name, mask in regions.items()}
    return result


def main():
    import cv2
    now = datetime.now(timezone.utc)
    prefix = 'analyse-neutres-rec2020'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    work = Path('build-colorimetrie-inventory') / campaign
    work.mkdir(parents=True, exist_ok=False)
    source_files = [Path(__file__), TRIAL, MANIFEST, EXE] + [Path(p) for p in (
        'tests/analysis/native.py', 'tests/inspect_saved_raw_state.py',
        'tests/compare_raw_radiometric_output.py', 'tests/analyse_base_colorimetrique_v2.py',
        'src/core/ColorPipeline.cpp', 'src/core/ColorPipeline.h', 'src/core/WorkingColorSpace.h',
        'src/editor/imageeditor.cpp', 'src/raw/RawDecoder.cpp', 'src/raw/RawNormalization.cpp',
        'src/profiles/CameraProfileEngine.cpp', 'tests/raw_normalization_corpus_probe.cpp',
        'tests/raw_opening_trial_helpers.h')]
    fingerprints = {str(p): native.sha256(p) for p in source_files}
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='base-colorimetrique', measured_at_utc=now.isoformat(), status='auditing',
        tools=dict(python=platform.python_version(), numpy=np.__version__, pillow=PIL.__version__, opencv=cv2.__version__),
        inputs=fingerprints, audits=[], records=[],
        method=dict(native_bits=16, sample_step=8, output='sRGB SDR', master='Rec.2020 linear D65',
                    geometry='Independent native grids, no registration or resized pixel comparison',
                    sign='100*(IPR/LR-1) for relative luminance; IPR-LR for percentage points',
                    replay_max_allowed_codes=4, preflight='All files before any LR/IPR comparison'),
        units=dict(Y='relative linear sRGB luminance', fractions='percent',
                   colour='CIE Lab D65 summary statistics, not paired colour differences'),
        limitations=['Current sidecar is not independently timestamped export metadata',
                     'Native replay checks the supplied manual TIFF against current neutral rendering',
                     'Independent geometry/FOV can bias global distributions and fixed normalized ROIs',
                     'No pixel MAE/DeltaE without registration, no physical colour target',
                     'One RAW decode per scene; not a latency or RSS benchmark',
                     'Clipped-code fractions do not prove lost sensor detail'])
    active = 'inventory'
    try:
        old = {r['scene']: r for r in native.read_json(TRIAL)['records']}
        manifest = native.read_json(MANIFEST)
        raws = sorted(BASE.glob('*.CR3'))
        if not raws or set(p.stem for p in raws) != set(old): raise ValueError('RAW corpus differs from the 19-scene reference')
        folder = BASE/'neutre'
        for raw in raws:
            active = raw.stem
            candidates = [p for p in folder.iterdir() if p.suffix.lower() in ('.tif', '.tiff') and
                          p.name.split('-')[0] == raw.stem and 'raw_audit' in p.name.lower()]
            if len(candidates) != 1: raise ValueError('Missing/ambiguous IPR export: '+str(candidates))
            ipr = candidates[0]
            state = sidecar(Path(str(raw)+'.ipr'))
            audit_saved(state, fingerprints[str(MANIFEST)])
            profile = Path(state['raw']['profile'])
            raw_hash, profile_hash = native.sha256(raw), native.sha256(profile)
            if profile_hash != state['raw']['profileSha256']: raise ValueError('Changed DCP')
            lr = Path(old[raw.stem]['LR_reference']['LR_file'])
            if not lr.exists(): lr = folder/(raw.stem+'.tif')
            metadata = {}
            for role, path in (('LR', lr), ('IPR', ipr)):
                active = str(path)
                pixels, meta, sig = native.inspect(path)
                if role == 'LR': audit_lr(meta)
                native.verify_unchanged(path, sig, meta['sha256'])
                metadata[role] = meta
                fingerprints[str(path)] = meta['sha256']
                del pixels
            fingerprints[str(raw)] = raw_hash
            fingerprints[str(profile)] = profile_hash
            fingerprints[state['file']] = state['sha256']
            result['audits'].append(dict(scene=raw.stem, source=str(raw), state=state,
                profile=str(profile), calibration={}, **metadata))
            print('AUDIT PASS', raw.stem, flush=True)

        # Only now replay the actual master; do not compare LR on a partial corpus.
        env = os.environ.copy()
        if os.name == 'nt':
            env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                                   'C:/dev/vcpkg/installed/x64-windows/bin', env.get('PATH', '')])
            env['QT_PLUGIN_PATH'] = 'C:/Qt/6.11.2/msvc2022_64/plugins'
        env['QT_QPA_PLATFORM'] = 'offscreen'
        for audit in result['audits']:
            active = audit['IPR']['path']
            out = work/audit['scene']
            print('REPLAY', audit['scene'], flush=True)
            subprocess.run([str(EXE.resolve()), str(Path(audit['source']).resolve()), audit['profile'],
                str(MANIFEST.resolve()), str(out.resolve()), '--working-space-source'], env=env,
                check=True, stdout=subprocess.DEVNULL, timeout=180)
            proof = native.read_json(out/'manifest.json')
            audit['calibration'] = dict(camera_model=proof['camera'], white_level=proof['white_level'],
                baseline_exposure_ev=proof['baseline_exposure_ev'],
                profile_exposure_compensation_ev=proof['profile_exposure_compensation_ev'])
            if proof.get('master_domain') != 'Rec.2020 linear D65 float32' or not proof['normalization_applied']:
                raise ValueError('Stale executable or calibration not applied')
            if proof['CA'] or proof['lens_geometry'] or proof['lens_vignetting'] or not proof['no_lcp']:
                raise ValueError('Optics unexpectedly applied')
            if audit['state']['settings']['temperature'] != round(proof['source_kelvin']):
                raise ValueError('Saved temperature is not the source neutral temperature')
            pixels, meta, _ = native.inspect(Path(active))
            witness = cv2.imread(str(out/'native-current.png'), cv2.IMREAD_UNCHANGED)[..., :3][..., ::-1]
            sample = np.asarray(pixels[::8, ::8, :3])
            if sample.shape != witness.shape: raise ValueError('Manual export/replay geometry mismatch')
            codes = np.abs(sample.astype(np.int32)-witness.astype(np.int32))
            replay = dict(max_16bit_codes=int(codes.max()), mean_abs_codes=float(codes.mean()),
                          nonidentical_samples_percent=float(100*np.mean(np.any(codes != 0, axis=-1))))
            audit['replay'] = dict(native=proof, difference=replay,
                                  proof_sha256=native.sha256(out/'manifest.json'))
            del pixels
            if replay['max_16bit_codes'] > 4: raise ValueError('Manual TIFF differs from neutral replay: '+str(replay))
            result['tools']['libraw'] = proof['backend_version']

        for audit in result['audits']:
            active = audit['scene']
            measured = {}
            for role in ('LR', 'IPR'):
                pixels, meta, sig = native.inspect(Path(audit[role]['path']))
                measured[role] = stats(pixels)
                native.verify_unchanged(Path(meta['path']), sig, meta['sha256'])
                del pixels
            a, b = measured['IPR'], measured['LR']
            deltas = dict(mean_Y_relative_to_LR_percent=100*(a['mean_Y']/b['mean_Y']-1),
                clipping_percentage_points=a['encoded_clipped_percent']-b['encoded_clipped_percent'],
                all_channels_high_percentage_points=a['all_channels_high_percent']-b['all_channels_high_percent'],
                Y_quantile_differences=(np.array(a['Y_p01_p10_p25_p50_p75_p90_p99'])-
                                        np.array(b['Y_p01_p10_p25_p50_p75_p90_p99'])).tolist())
            result['records'].append(dict(scene=audit['scene'], LR=measured['LR'], IPR=measured['IPR'], deltas=deltas))
            print('MEASURED', active, 'Y relative percent', round(deltas['mean_Y_relative_to_LR_percent'], 4), flush=True)
        for path, digest in fingerprints.items():
            if native.sha256(Path(path)) != digest: raise ValueError('Input changed: '+path)
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
