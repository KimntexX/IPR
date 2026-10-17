"""Read-only, registered neutral-export study; never installs a production gain.

Run from the repository root. Prior audited input hashes are mandatory.
Fitting is in exported linear sRGB, not RAW or pre-DCP exposure space.
"""
from datetime import datetime, timezone
from pathlib import Path
import platform

import cv2
import numpy as np

from analysis import native
from analyse_base_colorimetrique_v2 import srgb_lab, delta_e_2000

MEMORY = Path('docs/validation/raw/analyse-neutres-rec2020-v001-20261015T212901690091Z.json')
LUMA = np.array([.2126, .7152, .0722])


def encode(rgb):
    rgb = np.clip(rgb, 0, 1)
    return np.where(rgb <= .0031308, 12.92*rgb, 1.055*rgb**(1/2.4)-.055)


def register(lr, ipr):
    """Affine geometry on smoothed log-Y; template LR -> input IPR coordinates."""
    def features(rgb):
        y = rgb @ LUMA
        return cv2.GaussianBlur(np.log1p(20*y).astype(np.float32), (0, 0), 1.2)
    a, b = features(lr), features(ipr)
    transform = np.array([[1., 0., (b.shape[1]-a.shape[1])/2],
                          [0., 1., (b.shape[0]-a.shape[0])/2]], dtype=np.float32)
    interior = np.zeros(b.shape, np.uint8)
    interior[12:-12, 12:-12] = 255
    cc_translation, transform = cv2.findTransformECC(a, b, transform, cv2.MOTION_TRANSLATION,
        (cv2.TERM_CRITERIA_COUNT | cv2.TERM_CRITERIA_EPS, 120, 1e-7), interior, 5)
    translation = transform.copy()
    cc, transform = cv2.findTransformECC(a, b, transform, cv2.MOTION_AFFINE,
        (cv2.TERM_CRITERIA_COUNT | cv2.TERM_CRITERIA_EPS, 120, 1e-7), interior, 5)
    if cc < .97 or not np.allclose(transform[:, :2], np.eye(2), atol=.02):
        raise ValueError('Registration gate failed: '+str((cc, transform.tolist())))
    flags = cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP
    size = (a.shape[1], a.shape[0])
    aligned = cv2.warpAffine(ipr, transform, size, flags=flags)
    support = cv2.warpAffine(np.ones(b.shape, np.float32), transform, size, flags=flags) > .999
    support = cv2.erode(support.astype(np.uint8), np.ones((9, 9), np.uint8)).astype(bool)
    support[:8] = support[-8:] = False
    support[:, :8] = support[:, -8:] = False
    # Edge agreement is recorded separately from the optimized ECC score.
    warped = features(aligned)
    ga = cv2.Sobel(a, cv2.CV_32F, 1, 0) + cv2.Sobel(a, cv2.CV_32F, 0, 1)
    gb = cv2.Sobel(warped, cv2.CV_32F, 1, 0) + cv2.Sobel(warped, cv2.CV_32F, 0, 1)
    edge_correlation = float(np.corrcoef(ga[support], gb[support])[0, 1])
    if edge_correlation < .90: raise ValueError('Insufficient structural agreement')
    return aligned, support, dict(ecc=float(cc), translation_ecc=float(cc_translation),
        affine_LR_to_IPR_quarter_size=transform.tolist(), translation=translation.tolist(),
        edge_correlation=edge_correlation, overlap_fraction=float(support.mean()))


def fit_tiles(lr, ipr, support, tile=24):
    """Robust, equally weighted spatial tiles; suppress high-ISO pixel-ratio bias."""
    ylr, yipr = lr @ LUMA, ipr @ LUMA
    valid = support & (ylr > .01) & (yipr > .01) & (ylr < .65) & (yipr < .65)
    valid &= (lr.max(axis=-1) < .97) & (ipr.max(axis=-1) < .97)
    records = []
    for y in range(0, lr.shape[0]-tile+1, tile):
        for x in range(0, lr.shape[1]-tile+1, tile):
            sl = np.s_[y:y+tile, x:x+tile]
            mask = valid[sl]
            if mask.mean() < .8: continue
            a = lr[sl][mask].mean(axis=0, dtype=np.float64)
            b = ipr[sl][mask].mean(axis=0, dtype=np.float64)
            records.append(dict(x=x, y=y, samples=int(mask.sum()),
                LR_RGB=a.tolist(), IPR_RGB=b.tolist(), LR_Y=float(a@LUMA), IPR_Y=float(b@LUMA)))
    if len(records) < 30: raise ValueError('Insufficient valid spatial tiles')
    return records, valid


def gain(tiles):
    return float(np.exp(np.median([np.log(t['LR_Y']/t['IPR_Y']) for t in tiles])))


def score(lr, ipr, valid, coefficient):
    a, b = lr[valid].astype(np.float64), ipr[valid].astype(np.float64)*coefficient
    ya, yb = a@LUMA, b@LUMA
    lab_a, _ = srgb_lab(encode(a))
    lab_b, _ = srgb_lab(encode(b))
    de = delta_e_2000(lab_a, lab_b)
    return dict(mean_Y_bias_percent=float(100*(yb.mean()/ya.mean()-1)),
        MAE_Y=float(np.mean(np.abs(yb-ya))), median_DeltaE00=float(np.median(de)),
        p95_DeltaE00=float(np.percentile(de, 95)),
        mean_RGB_bias_percent=(100*(b.mean(axis=0)/a.mean(axis=0)-1)).tolist(),
        samples=int(valid.sum()))


def summarize_tiles(tiles):
    ratios = [t['LR_Y']/t['IPR_Y'] for t in tiles]
    result = dict(gain=gain(tiles), gain_p10_p50_p90=np.percentile(ratios, [10, 50, 90]).tolist(),
                  tiles=len(tiles), tonal_gains={})
    for name, lo, hi in [('dark', .01, .05), ('middle', .05, .2), ('bright', .2, .65)]:
        subset = [t for t in tiles if lo <= t['LR_Y'] < hi]
        result['tonal_gains'][name] = dict(tiles=len(subset), gain=gain(subset) if subset else None)
    result['channel_gains'] = np.exp(np.median([
        np.log(np.array(t['LR_RGB'])/t['IPR_RGB']) for t in tiles], axis=0)).tolist()
    return result


def main():
    now = datetime.now(timezone.utc)
    prefix = 'investigation-gain-r7'
    previous = sorted(Path('docs/validation/raw').glob(prefix+'-v[0-9][0-9][0-9]-*.json'))
    version = int(previous[-1].stem.split('-v')[-1][:3])+1 if previous else 1
    campaign = f'{prefix}-v{version:03d}-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    memory = native.read_json(MEMORY)
    fingerprints = {str(p): native.sha256(p) for p in (MEMORY, Path(__file__),
        Path('tests/analysis/native.py'), Path('tests/analyse_base_colorimetrique_v2.py'))}
    result = dict(campaign_id=campaign, schema_version=1, data_version=version,
        group='base-colorimetrique', measured_at_utc=now.isoformat(), status='auditing',
        inputs=fingerprints, records=[], tools=dict(python=platform.python_version(),
            numpy=np.__version__, opencv=cv2.__version__),
        method=dict(domain='exported linear sRGB; not RAW baseline EV',
            registration='log-Y ECC translation then affine, area-downsampled by four',
            fitting='median log LR/IPR luminance ratio of 24x24 spatial tiles; equal scene weights',
            fitting_mask='overlap; both Y .01 to .65; each channel below .97; tile valid >=80%',
            gates='ECC>=.97, affine within .02 of identity, gradient correlation>=.90, >=30 tiles',
            validation='leave-one-scene-out; no held-out scene contributes to its coefficient'),
        limitations=['Three high-ISO R7 scenes are not a universal camera calibration',
            'Affine registration and filtering cannot remove demosaicing or nonlinear lens differences',
            'Output gain cannot recover clipped details; not interchangeable with pre-DCP baseline exposure',
            'No production changes or image writes; paired metrics are filtered-grid measurements'])
    active = 'preflight'
    try:
        selected = [a for a in memory['audits'] if a['scene'].startswith('7Y6A') or
                    a['scene'] in ('6V1A0081', '6V1A0182', '6V1A9421')]
        # Verify all selected sources first, including prior RAW/profile/recipe context.
        for audit in selected:
            for path in [audit['source'], audit['profile'], audit['state']['file'],
                         audit['LR']['path'], audit['IPR']['path']]:
                active = path
                expected = memory['inputs'][path]
                if native.sha256(path) != expected: raise ValueError('Input changed since neutral audit: '+path)
                fingerprints[path] = expected
        arrays = {}
        for audit in selected:
            active = audit['scene']
            print('REGISTER', active, flush=True)
            rgb = {}
            for role in ('LR', 'IPR'):
                pixels, meta, sig = native.inspect(Path(audit[role]['path']))
                h, w = pixels.shape[:2]
                # Decode first, then average; avoid gamma-domain interpolation for radiometry.
                # Process in stripes to bound memory for 32MP TIFFs.
                linear = np.empty(((h+3)//4, (w+3)//4, 3), np.float32)
                for y in range(0, h, 256):
                    block = native.linearize(np.asarray(pixels[y:y+256, :, :3], np.float32)/65535)
                    bh = min(256, h-y)
                    linear[y//4:(y+bh+3)//4] = cv2.resize(block,
                        (linear.shape[1], (bh+3)//4), interpolation=cv2.INTER_AREA)
                native.verify_unchanged(Path(meta['path']), sig, meta['sha256'])
                rgb[role] = linear
                del pixels
            aligned, support, geometry = register(rgb['LR'], rgb['IPR'])
            # Identical spatial filtering reduces high-ISO noise and edge interpolation sensitivity.
            lr = cv2.GaussianBlur(rgb['LR'], (0, 0), 1.5)
            ipr = cv2.GaussianBlur(aligned, (0, 0), 1.5)
            tiles, valid = fit_tiles(lr, ipr, support)
            fitted = summarize_tiles(tiles)
            arrays[active] = (lr, ipr, valid)
            result['records'].append(dict(scene=active, camera=audit['calibration']['camera_model'],
                iso=audit['calibration']['iso'], calibration=audit['calibration'],
                profile=audit['profile'], geometry=geometry, fit=fitted, tiles=tiles,
                before=score(lr, ipr, valid, 1), self_fit=score(lr, ipr, valid, fitted['gain']),
                full_overlap_mean_Y_bias_percent=float(100*((ipr@LUMA)[support].mean()/
                                                          (lr@LUMA)[support].mean()-1)),
                historical_independent_mean_Y_bias_percent=next(r['deltas']['mean_Y_relative_to_LR_percent']
                    for r in memory['records'] if r['scene'] == active)))
            print('FIT', active, fitted['gain'], 'ECC', geometry['ecc'], flush=True)
        rows = [r for r in result['records'] if r['camera'] == 'EOS R7']
        common = float(np.exp(np.mean([np.log(r['fit']['gain']) for r in rows])))
        result['candidate'] = dict(linear_output_gain=common, equivalent_output_ev=float(np.log2(common)),
            scope='R7 / Camera Neutral / supplied high-ISO corpus; experimental, not installed')
        for row in result['records']:
            a, b, valid = arrays[row['scene']]
            if row['camera'] == 'EOS R7':
                others = [r['fit']['gain'] for r in rows if r is not row]
                holdout = float(np.exp(np.mean(np.log(others))))
                row['common_fit'] = score(a, b, valid, common)
                row['leave_one_out'] = dict(gain=holdout, **score(a, b, valid, holdout))
            else:
                row['R6_if_misapplied'] = score(a, b, valid, common)
        for path, digest in fingerprints.items():
            if native.sha256(path) != digest: raise ValueError('Input changed during study: '+path)
        result['status'] = 'complete'
        print('CANDIDATE', result['candidate'], flush=True)
        for r in rows:
            print(r['scene'], 'before', r['before'], 'LOO', r['leave_one_out'], flush=True)
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
