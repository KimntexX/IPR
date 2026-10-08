#!/usr/bin/env python3
"""Restricted, read-only DCP ordering / negative exposure / gamut experiment.

Camera Neutral A/D65, SDR, no HueSatMap, sRGB-encoded LookTable only.
This is NOT a full DNG SDK renderer or a Lightroom compatibility claim.
No profile table payload is persisted; no fitted exposure or curve coefficients.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyse_base_colorimetrique_v2 import compare, file_hash, srgb_lab
from analyse_canon_white_levels import load_stage
from analyse_color_mixer import native_pixels, linearize
from investigate_dcp_curve import PROPHOTO_TO_SRGB, curve, encoded, hue_stable
from investigate_tonal_base import (apply_linear_table, encode, hsv_to_rgb,
                                    local_path, read_dcp, rgb_to_hsv)

PROPHOTO_TO_XYZ = np.array([[.7976749, .1351917, .0313534],
                           [.2880402, .7118741, .0000857], [0, 0, .8252100]])
SDK_SOURCE = ('https://android.googlesource.com/platform/external/dng_sdk/+/'
              'de700ad461e35af50b28b861943a0b0753b10929/source/dng_render.cpp')


def look_table(rgb, dimensions, entries, encoding):
    """Only Value is encoded: Hue/Saturation are retained in linear ProPhoto."""
    if encoding not in (0, 1):
        raise ValueError('Unsupported encoding')
    if encoding == 0 or dimensions[2] == 1:
        return apply_linear_table(rgb, dimensions, entries)
    hsv = rgb_to_hsv(np.clip(rgb, 0, 1))
    hsv[..., 2] = encode(hsv[..., 2])
    mapped = apply_linear_table(hsv_to_rgb(hsv), dimensions, entries)
    hsv = rgb_to_hsv(mapped)
    hsv[..., 2] = linearize(hsv[..., 2])
    return hsv_to_rgb(hsv)


def negative_exposure(values, ev):
    """C1 quadratic shoulder: slope 2**ev below 1/4, maps white to white.

    Mathematical experiment described by dng_function_exposure_tone at the
    pinned SDK source. Does not include Stage3Gain, black ramp or interpolation.
    """
    if not np.isfinite(ev) or ev > 0 or ev < -16:
        raise ValueError('Expected finite negative exposure in [-16,0]')
    x = np.asarray(values, dtype=np.float64)
    if not np.isfinite(x).all() or np.any((x < 0) | (x > 1)):
        raise ValueError('Expected bounded finite values')
    s = 2.0 ** ev
    a = (1-s) / (.75**2)
    return np.where(x <= .25, s*x, s*x + a*(x-.25)**2)


def signed_gamut(rgb):
    """Ablation: preserve signed input luminance before chroma compression.

    Bounded neutral-axis compression, not a perceptual gamut mapping standard.
    """
    rgb = np.asarray(rgb, dtype=np.float64)
    y = rgb @ np.array([.2126, .7152, .0722])
    rgb = rgb / np.maximum(y, 1)[..., None]
    source_y = rgb @ np.array([.2126, .7152, .0722])
    target_y = np.clip(source_y, 0, 1)
    chroma = rgb-source_y[..., None]
    ratios = np.divide(np.where(chroma > 0, 1-target_y[..., None], target_y[..., None]),
                       np.abs(chroma), out=np.ones_like(chroma), where=chroma != 0)
    strength = np.clip(ratios.min(axis=-1), 0, 1)
    return np.clip(target_y[..., None]+chroma*strength[..., None], 0, 1)


def quantize(rgb):
    return np.floor(encode(rgb)*65535+.5)/65535


def require_profile(tags):
    if (tags.get(50778) != (17,) or tags.get(50779) != (21,) or
            any(tag in tags for tag in (50937, 50938, 50939, 52551)) or
            any(tag not in tags for tag in (50964, 50965, 50940, 50981, 50982)) or
            tags.get(51108) != (1,) or tags.get(51109) != (-.5,)):
        raise ValueError('Unsupported profile for this restricted Camera Neutral experiment')


def render(camera, kelvin, tags, camera_baseline=None):
    require_profile(tags)
    weight = np.clip((1/np.clip(kelvin, 1500, 50000)-1/2856)/(1/6500-1/2856), 0, 1)
    forward = ((1-weight)*np.array(tags[50964])+weight*np.array(tags[50965])).reshape(3, 3)
    # Match double matrix operations and float32 ProPhoto storage in C++.
    pre = (camera @ forward.T) @ np.linalg.inv(PROPHOTO_TO_XYZ).T
    scale = 2**tags[51109][0]
    apply_look = lambda rgb: look_table(rgb, tags[50981], tags[50982], tags[51108][0])
    before = apply_look((pre*scale).astype(np.float32).astype(np.float64))
    unscaled = apply_look(pre.astype(np.float32).astype(np.float64))
    points = tags[50940]
    channel = lambda rgb: curve(rgb, points)
    # Identical curve, one operation moved at a time. Post-LUT scalar is an
    # order ablation; SDK-shaped is a mathematical model, not SDK execution.
    transforms = {
        'production_order_reconstructed': channel(before),
        'offset_after_look_scalar': channel(unscaled*scale),
        'offset_after_look_shoulder': channel(negative_exposure(unscaled, -.5)),
        'offset_after_look_shoulder_hue_stable': hue_stable(negative_exposure(unscaled, -.5), points),
        'zero_offset_ablation': channel(unscaled),
    }
    if camera_baseline is not None:
        if not np.isfinite(camera_baseline) or abs(camera_baseline) > 16:
            raise ValueError('Invalid camera baseline')
        transforms['dng_baseline_plus_profile_offset'] = channel(
            apply_look((pre*2**(camera_baseline+tags[51109][0])).astype(np.float32).astype(np.float64)))
    return before @ PROPHOTO_TO_SRGB.T, {k: v @ PROPHOTO_TO_SRGB.T for k, v in transforms.items()}


def analyse(args):
    reference = json.loads(args.reference.read_text(encoding='utf-8'))
    camera_manifest = json.loads((args.camera_probe/'manifest.json').read_text(encoding='utf-8'))
    original = json.loads((args.original_probe/'manifest.json').read_text(encoding='utf-8'))
    no_curve = json.loads((args.no_curve_probe/'manifest.json').read_text(encoding='utf-8'))
    tags = read_dcp(args.profile)
    require_profile(tags)
    dng_audit = json.loads(args.dng_audit.read_text(encoding='utf-8')) if args.dng_audit else None
    result = {'schema': 1, 'diagnostic_only': True, 'sdk_source': SDK_SOURCE,
        'limitations': 'Restricted independent reproduction, not SDK/Lightroom execution. No sensor baseline exposure, Stage3Gain or black ramp inferred from the profile. Linear interpolation held fixed. No fitting. Signed gamut is only an ablation.',
        'analyser_sha256': file_hash(Path(__file__)),
        'dependencies_sha256': {name: file_hash(Path(__file__).with_name(name+'.py')) for name in
            ('investigate_dcp_curve', 'investigate_tonal_base', 'analyse_canon_white_levels',
             'analyse_base_colorimetrique_v2', 'analyse_color_mixer')},
        'inputs_sha256': {str(p): file_hash(p) for p in (args.reference, args.profile,
            args.camera_probe/'manifest.json', args.original_probe/'manifest.json', args.no_curve_probe/'manifest.json')},
        'profile_has_hue_sat_map': False, 'offset_ev': -.5, 'scenes': []}
    if args.dng_audit:
        result['inputs_sha256'][str(args.dng_audit)] = file_hash(args.dng_audit)
    for ref in reference['scenes']:
        neutral = ref['camera_neutral_reference']
        scene = ref['scene']
        if not neutral['available']:
            result['scenes'].append({'scene': scene, 'available': False})
            continue
        def choose(manifest, condition):
            items = [x for x in manifest['images'] if Path(x['raw']).stem == scene and condition(x)]
            if len(items) != 1:
                raise ValueError('Expected unique scene witness: '+scene)
            return items[0]
        source = choose(camera_manifest, lambda x: x['variant'] == 'baseline')
        old = choose(original, lambda x: x['geometry']=='without_lcp' and 'Camera Neutral.dcp' in x['profile'])
        noc = choose(no_curve, lambda x: x['geometry']=='without_lcp')
        if (file_hash(args.profile) != old['profile_sha256'] or
                source['stages']['camera_rgb']['sha256'] != old['source_camera_sha256'] or
                old['source_camera_sha256'] != noc['source_camera_sha256'] or
                source['wb_kelvin'] != old['wb_kelvin'] or old['wb_kelvin'] != noc['wb_kelvin']):
            raise ValueError('Profile/camera/WB provenance mismatch')
        camera = load_stage(args.camera_probe, source, 'camera_rgb')
        camera_baseline = None
        if dng_audit:
            dng = next(x for x in dng_audit['scenes'] if x['scene'] == scene)
            if dng['raw_sha256'] != source['raw_sha256'] or file_hash(local_path(dng['path'])) != dng['sha256']:
                raise ValueError('DNG/RAW provenance mismatch')
            camera_baseline = dng['ifd0']['baseline_exposure']
            if camera_baseline is None:
                raise ValueError('DNG baseline absent')
        pre_curve, variants_linear = render(camera, source['wb_kelvin'], tags, camera_baseline)
        witness_pre = load_stage(args.no_curve_probe, noc, 'dcp_linear')
        baseline = load_stage(args.original_probe, old, 'encoded_srgb')
        errors = np.abs(encoded(variants_linear['production_order_reconstructed'])-baseline)*65535
        pre_error = float(np.abs(pre_curve-witness_pre).max())
        if pre_error > 1e-5 or errors.max() > 4.000001 or errors.mean() > .03:
            raise ValueError(f'{scene}: independent witness mismatch {pre_error}, {errors.max()}, {errors.mean()}')
        meta = neutral['lr_metadata']
        path = local_path(meta['path'])
        if file_hash(path) != meta['sha256']:
            raise ValueError('LR reference changed')
        crop = neutral['crop_candidates'][0]
        pixels = native_pixels(path)
        if pixels.dtype != np.uint16:
            raise ValueError('Expected 16-bit reference')
        h, w = baseline.shape[:2]
        lr = pixels[crop['top']::8, crop['left']::8, :3][:h, :w].astype(float)/65535
        if lr.shape != baseline.shape:
            raise ValueError('Reference geometry mismatch')
        baseline_metrics = compare(baseline, lr)
        if abs(baseline_metrics['delta_e00_mean']-neutral['same_profile_vs_lr']['delta_e00_mean']) > 1e-10:
            raise ValueError('Historical metric/crop mismatch')
        y = srgb_lab(lr)[1]
        masks = {'lr_dark_y_le_002': y <= .02, 'lr_highlight_top5': y >= np.percentile(y, 95)}
        production_linear = load_stage(args.original_probe, old, 'dcp_linear')
        variants = {'production': baseline, **{k: encoded(v) for k, v in variants_linear.items()},
            'production_hard_clip_gamut': quantize(production_linear),
            'production_signed_gamut': quantize(signed_gamut(production_linear))}
        record = {'scene': scene, 'available': True, 'samples': h*w, 'lr_sha256': file_hash(path),
            'dng_camera_baseline_ev': camera_baseline,
            'camera_sha256': source['stages']['camera_rgb']['sha256'],
            'witness_pre_curve_max_abs': pre_error, 'witness_max_code_error': float(errors.max()),
            'witness_mean_code_error': float(errors.mean()),
            'production_linear_negative_fraction': float(np.any(production_linear < 0, axis=-1).mean()),
            'variants': {k: {'vs_lr': compare(v, lr),
                'regions': {key: compare(v[mask], lr[mask]) for key, mask in masks.items() if mask.any()}}
                for k, v in variants.items()}}
        result['scenes'].append(record)
        print(scene, 'DE00:', {k: round(v['vs_lr']['delta_e00_mean'], 4) for k,v in record['variants'].items()}, flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('profile', 'camera-probe', 'original-probe', 'no-curve-probe', 'reference', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--dng-audit', type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite a measurement')
    result = analyse(args)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
