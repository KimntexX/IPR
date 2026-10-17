"""Native R7 baseline-exposure sweep, private diagnostics only, no production edits."""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

import cv2
import numpy as np

from analysis import native
from investigate_r7_gain import MEMORY, LUMA, fit_tiles, score

EXE = Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')
CONFIG = Path('config/raw-normalization.json')
BASELINES = (.30, .32, .33, .34, .35)


def main():
    now = datetime.now(timezone.utc)
    prefix = 'investigation-baseline-r7'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    study_path = sorted(Path('docs/validation/raw').glob('investigation-gain-r7-v*.json'))[-1]
    study, memory = native.read_json(study_path), native.read_json(MEMORY)
    work = Path('build-colorimetrie-inventory')/campaign
    work.mkdir(exist_ok=False)
    files = [Path(__file__), Path('tests/investigate_r7_gain.py'), EXE, CONFIG, MEMORY, study_path,
             Path('src/profiles/CameraProfileEngine.cpp'), Path('src/raw/RawDecoder.cpp')]
    hashes = {str(p): native.sha256(p) for p in files}
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='base-colorimetrique', measured_at_utc=now.isoformat(), inputs=hashes,
        status='auditing', records=[], method=dict(
            domain='Production cameraBaselineExposureEv before DCP tables/curve',
            candidates_ev=list(BASELINES), sample_step=4,
            geometry='Fixed affine from independently registered TIFF study; same support/mask for all EV',
            filtering='Full production 16bit output decoded to linear RGB, INTER_AREA /4, Gaussian sigma 1.5; tiles 24x24',
            fitting='Equal-scene mean normalized luminance MAE; leave-one-scene-out EV selection'),
        limitations=['No production config or photos changed; diagnostic manifests are isolated',
                     'Only three R7 at ISO20000/32000; no universal camera coefficient',
                     'Exposure changes before DCP are not equivalent to an output gain',
                     'Full-image clipped fractions are independent grids, not recovered detail',
                     'Registration/filtering leaves demosaic/noise and local-rendering differences'])
    active = 'preflight'
    try:
        if study['status'] != 'complete': raise ValueError('Incomplete registration study')
        audits = [a for a in memory['audits'] if a['scene'].startswith('7Y6A')]
        by_scene = {r['scene']: r for r in study['records']}
        for a in audits:
            for p in [a['source'], a['profile'], a['LR']['path'], a['IPR']['path'], a['state']['file']]:
                active = p
                if native.sha256(p) != memory['inputs'][p]: raise ValueError('Changed neutral audit input')
                hashes[p] = memory['inputs'][p]
        env = os.environ.copy()
        env['PATH'] = ';'.join(['C:/Qt/6.11.2/msvc2022_64/bin', 'C:/opencv/build/x64/vc16/bin',
                               'C:/dev/vcpkg/installed/x64-windows/bin', env.get('PATH', '')])
        env['QT_QPA_PLATFORM'] = 'offscreen'
        config = native.read_json(CONFIG)
        for a in audits:
            active = a['scene']
            pixels, meta, sig = native.inspect(Path(a['LR']['path']))
            # Lightroom R7 dimensions are divisible by four: exact 4x4 area means.
            h, w = pixels.shape[:2]
            if h % 4 or w % 4: raise ValueError('Expected R7 LR dimensions divisible by four')
            lr = np.empty((h//4, w//4, 3), np.float32)
            for y in range(0, h, 256):
                block = native.linearize(np.asarray(pixels[y:y+256, :, :3], np.float32)/65535)
                lr[y//4:(y+block.shape[0])//4] = cv2.resize(block,
                    (w//4, block.shape[0]//4), interpolation=cv2.INTER_AREA)
            native.verify_unchanged(Path(meta['path']), sig, meta['sha256'])
            del pixels
            lr = cv2.GaussianBlur(lr, (0, 0), 1.5)
            transform = np.array(by_scene[active]['geometry']['affine_LR_to_IPR_quarter_size'], np.float32)
            flags = cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP
            shape = (lr.shape[1], lr.shape[0])
            proof0 = a['replay']['native']
            curves = {}
            valid = None
            # Baseline first establishes a single mask independent of all candidate outputs.
            for ev in (.35, .30, .32, .33, .34):
                altered = dict(config, profile_rendering=[], entries=[dict(entry,
                    baseline_exposure_ev=ev) if entry['camera_model'] == 'EOS R7'
                    else entry for entry in config['entries']])
                manifest = work/f'{active}-ev-{ev:.2f}.json'
                native.write_immutable(manifest, altered)
                out = work/f'{active}-ev-{ev:.2f}'
                print('NATIVE', active, ev, flush=True)
                subprocess.run([str(EXE.resolve()), str(Path(a['source']).resolve()), a['profile'],
                    str(manifest.resolve()), str(out.resolve()), '--working-space-area'], env=env,
                    check=True, stdout=subprocess.DEVNULL, timeout=180)
                proof = native.read_json(out/'manifest.json')
                image_path = out/'native-current.png'
                if ev == .35 and native.sha256(image_path) != proof0['native_preview_sha256']:
                    raise ValueError('Changed baseline witness')
                if (not proof['normalization_applied'] or proof['baseline_exposure_ev'] != ev or
                        proof['master_domain'] != 'Rec.2020 linear D65 float32' or
                        proof['profile_sha256'] != a['calibration']['profile_sha256'] or
                        proof['CA'] or proof['lens_geometry'] or proof['lens_vignetting']):
                    raise ValueError('Incompatible native candidate')
                png = cv2.imread(str(image_path), cv2.IMREAD_UNCHANGED)[..., :3][..., ::-1]
                area = proof['output_area']
                area_path = out/'output-area-linear-rgb.f32'
                if native.sha256(area_path) != area['sha256']: raise ValueError('Changed area output')
                dtype = '<f4' if proof['byte_order'] == 'little' else '>f4'
                rgb = np.fromfile(area_path, dtype=dtype).reshape(area['rows'], area['cols'], 3)
                aligned = cv2.warpAffine(rgb, transform, shape, flags=flags)
                aligned = cv2.GaussianBlur(aligned, (0, 0), 1.5)
                if valid is None:
                    support = cv2.warpAffine(np.ones(rgb.shape[:2], np.float32), transform,
                        shape, flags=flags) > .999
                    support = cv2.erode(support.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
                    support[:8] = support[-8:] = False
                    support[:, :8] = support[:, -8:] = False
                    _, valid = fit_tiles(lr, aligned, support, tile=24)
                metrics = score(lr, aligned, valid, 1)
                if ev == .35 and abs(metrics['mean_Y_bias_percent']-by_scene[active]['before']['mean_Y_bias_percent']) > .01:
                    raise ValueError('Area replay disagrees with registered manual TIFF baseline')
                metrics['normalized_MAE_Y_percent'] = 100*metrics['MAE_Y']/float((lr@LUMA)[valid].mean())
                metrics['independent_clipped_percent'] = float(100*np.mean(np.any(png >= 65534, axis=-1)))
                metrics['proof'] = proof
                metrics['proof_sha256'] = native.sha256(out/'manifest.json')
                metrics['image_sha256'] = native.sha256(image_path)
                curves[f'{ev:.2f}'] = metrics
                print('SCORE', active, ev, metrics['mean_Y_bias_percent'],
                    metrics['normalized_MAE_Y_percent'], flush=True)
            result['records'].append(dict(scene=active, iso=a['calibration']['iso'], variants=curves))
        rows = result['records']
        def objective(training, ev):
            return float(np.mean([r['variants'][f'{ev:.2f}']['normalized_MAE_Y_percent'] for r in training]))
        result['aggregate'] = {f'{ev:.2f}': dict(mean_normalized_MAE_Y_percent=objective(rows, ev),
            mean_abs_Y_bias_percent=float(np.mean([abs(r['variants'][f'{ev:.2f}']['mean_Y_bias_percent']) for r in rows])),
            mean_median_DeltaE00=float(np.mean([r['variants'][f'{ev:.2f}']['median_DeltaE00'] for r in rows])))
            for ev in BASELINES}
        result['best_baseline_ev'] = min(BASELINES, key=lambda ev: objective(rows, ev))
        for r in rows:
            others = [s for s in rows if s is not r]
            selected = min(BASELINES, key=lambda ev: objective(others, ev))
            r['leave_one_out'] = dict(selected_baseline_ev=selected, held_out=r['variants'][f'{selected:.2f}'])
        for path, digest in hashes.items():
            if native.sha256(path) != digest: raise ValueError('Changed input during study: '+path)
        result['status'] = 'complete'
        print('AGGREGATE', result['aggregate'], 'BEST', result['best_baseline_ev'], flush=True)
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
