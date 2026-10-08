#!/usr/bin/env python3
"""Isolated Camera Neutral curve experiment. Never modifies production or Adobe files.

Prepare a LOCAL diagnostic DCP whose curve entry is hidden; run tonalBaseProbe
on it, then reapply the original curve with independent/channel or hue-stable
operators. All other DCP bytes and all source images remain unchanged.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np

from analyse_base_colorimetrique_v2 import compare, file_hash, srgb_lab
from analyse_canon_white_levels import load_stage
from analyse_color_mixer import native_pixels
from investigate_tonal_base import D50_TO_SRGB, encode, local_path, read_dcp

# Exact rounded constants used by CameraProfileEngine, not the ICC-derived
# higher precision matrix in investigate_tonal_base (witness must match IPR).
PROPHOTO_TO_SRGB = D50_TO_SRGB @ np.array([
    [.7976749, .1351917, .0313534], [.2880402, .7118741, .0000857], [0, 0, .8252100]])


def hide_curve(data):
    tags = read_header(data)
    entries = [entry for tag, entry in tags if tag == 50940]
    if len(entries) != 1 or any(tag == 65000 for tag, _ in tags):
        raise ValueError('Expected exactly one tone curve and no diagnostic tag')
    endian = '<' if data[:2] == b'II' else '>'
    result = bytearray(data)
    # Only the tag identifier changes. No offsets or data payload move.
    struct.pack_into(endian + 'H', result, entries[0], 65000)
    return bytes(result)


def read_header(data):
    if len(data) < 8 or data[:2] not in (b'II', b'MM'):
        raise ValueError('Invalid DCP header')
    endian = '<' if data[:2] == b'II' else '>'
    if struct.unpack_from(endian+'H', data, 2)[0] != 0x4352:
        raise ValueError('Not a DCP')
    offset = struct.unpack_from(endian+'I', data, 4)[0]
    count = struct.unpack_from(endian+'H', data, offset)[0]
    if offset + 2 + 12*count + 4 > len(data):
        raise ValueError('Truncated DCP directory')
    return [(struct.unpack_from(endian+'H', data, offset+2+12*i)[0], offset+2+12*i)
            for i in range(count)]


def curve(values, points, cubic=False):
    points = np.asarray(points).reshape(-1, 2)
    x, y = points.T
    if (len(x) < 2 or not np.isfinite(points).all() or
            np.any(np.diff(x) <= 0) or
            x[0] != 0 or x[-1] != 1 or np.any((y < 0) | (y > 1))):
        raise ValueError('Expected bounded curve with strictly increasing abscissas covering [0,1]')
    v = np.clip(values, 0, 1)
    if not cubic:
        return np.interp(v, x, y)
    # Shape-preserving cubic Hermite; independent interpolation experiment, NOT a
    # claim to reproduce Adobe's spline implementation.
    h, d = np.diff(x), np.diff(y)/np.diff(x)
    slopes = np.zeros(len(x))
    slopes[0], slopes[-1] = d[0], d[-1]
    for i in range(1, len(x)-1):
        if d[i-1]*d[i] > 0:
            w1, w2 = 2*h[i]+h[i-1], h[i]+2*h[i-1]
            slopes[i] = (w1+w2)/(w1/d[i-1]+w2/d[i])
    j = np.clip(np.searchsorted(x, v, side='right')-1, 0, len(x)-2)
    t = (v-x[j])/h[j]
    return ((2*t**3-3*t**2+1)*y[j] + (t**3-2*t**2+t)*h[j]*slopes[j]
            + (-2*t**3+3*t**2)*y[j+1] + (t**3-t**2)*h[j]*slopes[j+1])


def hue_stable(rgb, points, cubic=False):
    """Map RGB extrema and interpolate middle by its original relative position.

    Preserves HSV hue in ProPhoto before gamut mapping, not Lab hue/chroma.
    Derived directly from min/max interpolation, not copied GPL source code.
    """
    rgb = np.clip(rgb, 0, 1)
    low, high = rgb.min(axis=-1), rgb.max(axis=-1)
    position = np.divide(rgb-low[..., None], (high-low)[..., None],
                         out=np.zeros_like(rgb), where=(high > low)[..., None])
    a, b = curve(low, points, cubic), curve(high, points, cubic)
    return a[..., None] + position*(b-a)[..., None]


def gamut(rgb):
    """Float32 transcription of the production SDR gamut map, witness checked."""
    rgb = np.maximum(np.asarray(rgb, dtype=np.float32), np.float32(0))
    y = rgb[..., 0]*np.float32(.2126) + rgb[..., 1]*np.float32(.7152) + rgb[..., 2]*np.float32(.0722)
    scale = np.divide(np.float32(1), y, out=np.ones_like(y), where=y > 1)
    rgb = rgb*scale[..., None]
    y = np.minimum(y, np.float32(1))
    chroma = rgb-y[..., None]
    ratios = np.divide(np.where(chroma > 0, 1-y[..., None], y[..., None]),
                       np.abs(chroma), out=np.ones_like(chroma), where=chroma != 0)
    strength = np.clip(ratios.min(axis=-1), 0, 1)
    return y[..., None] + chroma*strength[..., None]


def encoded(rgb):
    # Probe calls mapToSrgbGamut explicitly; toSrgb calls it a second time.
    return np.floor(encode(gamut(gamut(rgb)).astype(np.float64))*65535+.5)/65535


def analyse(folder, original_folder, reference_path, profile, diagnostic):
    ref = json.loads(reference_path.read_text(encoding='utf-8'))
    manifest = json.loads((folder/'manifest.json').read_text(encoding='utf-8'))
    original = json.loads((original_folder/'manifest.json').read_text(encoding='utf-8'))
    if diagnostic.read_bytes() != hide_curve(profile.read_bytes()):
        raise ValueError('Diagnostic profile differs beyond the curve tag')
    points = read_dcp(profile)[50940]
    result = {'schema': 1, 'diagnostic_only': True,
        'profile_sha256': file_hash(profile), 'diagnostic_profile_sha256': file_hash(diagnostic),
        'reference_sha256': file_hash(reference_path), 'manifest_sha256': file_hash(folder/'manifest.json'),
        'original_manifest_sha256': file_hash(original_folder/'manifest.json'),
        'method': 'Fixed previous neutral crop, native step-8 grids; no fitting, no exposure/WB change. Float32 linear-sRGB -> ProPhoto before explicit curve. Gamut map checked against production witness.',
        'curve_points': len(points)//2,
        'curve_decreasing_segments': int(np.sum(np.diff(np.asarray(points)[1::2]) < 0)),
        'curve_min_ordinate_increment': float(np.min(np.diff(np.asarray(points)[1::2]))),
        'scenes': []}
    for row in ref['scenes']:
        neutral = row['camera_neutral_reference']
        if not neutral['available']:
            result['scenes'].append({'scene': row['scene'], 'available': False})
            continue
        scene = row['scene']
        def entry_for(items):
            return next(x for x in items if Path(x['raw']).stem == scene and x['geometry'] == 'without_lcp')
        new = entry_for(manifest['images'])
        old = entry_for([x for x in original['images'] if 'Camera Neutral.dcp' in x['profile']])
        if (file_hash(local_path(new['profile'])) != new['profile_sha256'] or
                file_hash(local_path(old['profile'])) != old['profile_sha256'] or
                old['profile_sha256'] != result['profile_sha256'] or
                new['profile_sha256'] != result['diagnostic_profile_sha256'] or
                new['source_camera_sha256'] != old['source_camera_sha256'] or new['wb_kelvin'] != old['wb_kelvin']):
            raise ValueError('Profile/camera/WB witness mismatch')
        pre = load_stage(folder, new, 'dcp_linear') @ np.linalg.inv(PROPHOTO_TO_SRGB).T
        baseline = load_stage(original_folder, old, 'encoded_srgb')
        reconstructed = encoded(np.asarray(curve(pre, points), dtype=np.float32) @ PROPHOTO_TO_SRGB.T)
        errors = np.abs(reconstructed-baseline)*65535
        # Inverse matrix of a float32 intermediate introduces tiny roundoff.
        if errors.max() > 2.000001 or errors.mean() > .02:
            raise ValueError(f'{scene}: independent witness does not reproduce production: {errors.max()}, {errors.mean()}')
        meta = neutral['lr_metadata']
        path = local_path(meta['path'])
        if file_hash(path) != meta['sha256']:
            raise ValueError('LR reference changed')
        height, width = baseline.shape[:2]
        offset = neutral['crop_candidates'][0]
        # Grid endpoints are not the full image dimensions. Use historical
        # IPR dimensions encoded by the original measurement's matching scene.
        pixels = native_pixels(path)
        if pixels.dtype.itemsize != 2:
            raise ValueError('Expected native 16-bit TIFF')
        lr = pixels[offset['top']::8, offset['left']::8, :3][:height, :width]/65535
        if lr.shape != baseline.shape:
            raise ValueError('LR grid dimensions mismatch')
        _, y = srgb_lab(lr)
        masks = {'lr_dark_y_le_002': y <= .02, 'lr_highlight_top5': y >= np.percentile(y, 95)}
        variants = {'production_per_channel_linear': baseline,
            'per_channel_shape_preserving_cubic': encoded(curve(pre, points, True) @ PROPHOTO_TO_SRGB.T),
            'hue_stable_linear': encoded(hue_stable(pre, points) @ PROPHOTO_TO_SRGB.T),
            'hue_stable_shape_preserving_cubic': encoded(hue_stable(pre, points, True) @ PROPHOTO_TO_SRGB.T),
            'no_curve_diagnostic': load_stage(folder, new, 'encoded_srgb')}
        baseline_metrics = compare(baseline, lr)
        if abs(baseline_metrics['delta_e00_mean']-neutral['same_profile_vs_lr']['delta_e00_mean']) > 1e-10:
            raise ValueError('Baseline/crop does not reproduce the historical comparison')
        extrema = np.clip(pre, 0, 1)
        reversed_extrema = curve(extrema.max(axis=-1), points) < curve(extrema.min(axis=-1), points)
        record = {'scene': scene, 'available': True, 'lr_sha256': file_hash(path),
            'hue_operator_reversed_extrema_fraction': float(reversed_extrema.mean()),
            'witness_max_code_error': float(errors.max()), 'witness_mean_code_error': float(errors.mean()),
            'variants': {name: {'vs_lr': compare(value, lr),
                'regions': {key: compare(value[mask], lr[mask]) for key, mask in masks.items() if mask.any()}}
                for name, value in variants.items()}}
        result['scenes'].append(record)
        print(scene, 'curve variants complete', flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--diagnostic-profile', type=Path, required=True)
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--original-probe', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.prepare:
        args.diagnostic_profile.parent.mkdir(parents=True, exist_ok=True)
        with args.diagnostic_profile.open('xb') as stream:
            stream.write(hide_curve(args.profile.read_bytes()))
        print('Local diagnostic profile prepared; installed source untouched')
    else:
        if not all((args.probe, args.original_probe, args.reference, args.output)):
            parser.error('Analysis requires probe, original-probe, reference and output')
        if args.output.exists():
            parser.error('Refuse to overwrite proof')
        result = analyse(args.probe, args.original_probe, args.reference, args.profile, args.diagnostic_profile)
        result['analyser_sha256'] = file_hash(Path(__file__))
        with args.output.open('x', encoding='utf-8') as stream:
            json.dump(result, stream, indent=2, allow_nan=False)


if __name__ == '__main__':
    main()
