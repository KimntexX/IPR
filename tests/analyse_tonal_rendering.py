#!/usr/bin/env python3
"""Diagnostic profile/geometry ablation and leave-one-scene-out tonal models.

Fitted transforms are experiments, never production settings or Lightroom truth.
"""
import argparse
import json
import math
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

from analyse_base_colorimetrique_v2 import audit_tiff, compare, delta_e_2000, file_hash, srgb_lab, strict_region
from analyse_color_mixer import native_pixels, linearize, edge_signature
from analyse_canon_white_levels import load_stage
from investigate_tonal_base import encode, local_path, profile_summary


def fit_model(pairs, power=False):
    """Equal scene weights, deterministic samples, robust log-domain fit."""
    selected = []
    for first, second in pairs:
        _, y1 = srgb_lab(first)
        _, y2 = srgb_lab(second)
        mask = (y1 > .002) & (y2 > .002) & (y1 < .9) & (y2 < .9)
        mask &= (first.max(axis=-1) < .999) & (second.max(axis=-1) < .999)
        a, b = np.log(y1[mask]), np.log(y2[mask])
        if len(a) < 64:
            raise ValueError('Insufficient unsaturated fitting samples')
        selected.append((a, b))
    count = min(4000, min(len(a) for a, _ in selected))
    x = np.concatenate([a[np.linspace(0, len(a)-1, count, dtype=int)] for a, _ in selected])
    y = np.concatenate([b[np.linspace(0, len(b)-1, count, dtype=int)] for _, b in selected])
    gamma = 1.
    if power:
        design = np.column_stack((np.ones(len(x)), x))
        keep = np.ones(len(x), dtype=bool)
        for _ in range(4):
            intercept, gamma = np.linalg.lstsq(design[keep], y[keep], rcond=None)[0]
            residual = y - intercept - gamma*x
            middle = np.median(residual)
            mad = np.median(np.abs(residual-middle))
            keep = np.abs(residual-middle) <= max(2.5*1.4826*mad, 1e-8)
        gamma = float(np.clip(gamma, .4, 1.6))
    intercept = float(np.clip(np.median(y-gamma*x), math.log(.25), math.log(8)))
    return {'gain': math.exp(intercept), 'gamma': gamma,
            'training_scenes': len(pairs), 'samples_per_scene': count,
            'gain_bounds': [.25, 8], 'gamma_bounds': [.4, 1.6]}


def apply_model(encoded, model):
    rgb = linearize(np.asarray(encoded, dtype=np.float64))
    _, y = srgb_lab(encoded)
    target = model['gain'] * y ** model['gamma']
    scale = np.divide(target, y, out=np.zeros_like(y), where=y > 0)
    mapped = rgb * scale[..., None]
    overshoot = float(np.any(mapped > 1, axis=-1).mean())
    output = np.floor(encode(mapped)*65535+.5)/65535
    return output, overshoot


def xmp_render_audit(path):
    with Image.open(path) as image:
        packet = image.tag_v2.get(700, b'')
    if isinstance(packet, tuple):
        packet = bytes(packet)
    if not packet:
        return {}
    root = ET.fromstring(packet)
    result = {}
    for node in root.iter():
        for key, value in node.attrib.items():
            name = key.split('}')[-1]
            if any(word in name for word in ('Profile', 'ToneCurve', 'ProcessVersion', 'WhiteBalance', 'Temperature', 'Tint', 'Exposure')):
                result[name] = value
        name = node.tag.split('}')[-1]
        if name.startswith('ToneCurve'):
            result[name] = [child.text for child in node.iter() if child.tag.endswith('}li')]
    return result


def harmonized_warnings(settings, expected_profile='Adobe Standard'):
    warnings = []
    for name, value in {'WhiteBalance': 'As Shot', 'CameraProfile': expected_profile,
                        'ToneCurveName2012': 'Linear'}.items():
        if settings.get(name) != value:
            warnings.append(f'{name}: expected {value}, got {settings.get(name)}')
    for name in ('Exposure2012', 'Contrast2012', 'Highlights2012', 'Shadows2012', 'Whites2012',
                 'Blacks2012', 'Texture', 'Clarity2012', 'Dehaze', 'Vibrance', 'Saturation',
                 'Sharpness', 'LuminanceSmoothing', 'ColorNoiseReduction', 'LensProfileEnable', 'AutoLateralCA'):
        try:
            if float(settings[name]) != 0:
                warnings.append(f'{name}: expected 0, got {settings[name]}')
        except (KeyError, ValueError):
            warnings.append(f'{name}: absent or invalid')
    return warnings


def grid_crop_candidates(reference, target_grid, width, height, step=8):
    dh, dw = reference.shape[0]-height, reference.shape[1]-width
    if dh not in (0, 1) or dw not in (0, 1):
        raise ValueError('Unexpected dimensions; no silent registration')
    edges = edge_signature(target_grid)
    result = []
    for top in range(dh+1):
        for left in range(dw+1):
            candidate = strict_region(reference, [0, 0, width, height], step, top, left)
            result.append({'top': top, 'left': left, 'edge_mae': float(np.abs(edge_signature(candidate)-edges).mean())})
    return sorted(result, key=lambda row: row['edge_mae'])


def analyse(folder, reference_path, lr_suffix='', geometry='with_lcp', neutral_suffix=None):
    manifest = json.loads((folder/'manifest.json').read_text(encoding='utf-8'))
    reference = json.loads(reference_path.read_text(encoding='utf-8'))
    result = {'schema': 1, 'profile_manifest_sha256': file_hash(folder/'manifest.json'),
              'reference_sha256': file_hash(reference_path),
              'lr_suffix': lr_suffix, 'baseline_geometry': geometry,
              'neutral_suffix': neutral_suffix,
              'method': 'Native grid; 0/1 pixel crop only, selected against baseline grid for new LR suffix, historical v3 crop otherwise. No optical registration. Profile variants use actual engine. LOO fits on five scenes, evaluation on sixth. Lab-L substitution is an oracle, not a render.',
              'profiles': [], 'scenes': []}
    for path in sorted({x['profile'] for x in manifest['images']}):
        result['profiles'].append(profile_summary(local_path(path)))
    pairs = []
    for ref in reference['scenes']:
        scene = ref['scene']
        entries = [x for x in manifest['images'] if Path(x['raw']).stem == scene]
        if len(entries) != 6:
            raise ValueError('Expected three profiles with/without LCP')
        baseline = next(x for x in entries if 'Adobe Standard.dcp' in x['profile'] and x['geometry'] == geometry)
        first = load_stage(folder, baseline, 'encoded_srgb')
        if any(file_hash(Path(ref[key]['path'])) != ref[key]['sha256'] for key in ('ipr', 'lr')):
            raise ValueError('TIFF reference changed')
        width, height = ref['ipr']['size']
        crop = ref['crop_candidates'][0]
        old_lr = strict_region(native_pixels(Path(ref['lr']['path'])), [0, 0, width, height], 8, crop['top'], crop['left'])
        lr_path = Path(ref['lr']['path']).with_name(scene + lr_suffix + '.tif')
        lr_meta = audit_tiff(lr_path)
        lr_pixels = native_pixels(lr_path)
        crops = grid_crop_candidates(lr_pixels, first, width, height) if lr_suffix else ref['crop_candidates']
        crop = crops[0]
        lr = strict_region(lr_pixels, [0, 0, width, height], 8, crop['top'], crop['left'])
        ipr = strict_region(native_pixels(Path(ref['ipr']['path'])), [0, 0, width, height], 8)
        witness = next(x for x in entries if 'Adobe Standard.dcp' in x['profile'] and x['geometry'] == 'with_lcp')
        if not np.array_equal(load_stage(folder, witness, 'encoded_srgb'), ipr):
            raise ValueError('Profile experiment baseline does not exactly reproduce v3 grid')
        rows = []
        for entry in entries:
            if file_hash(local_path(entry['profile'])) != entry['profile_sha256']:
                raise ValueError('DCP changed since experiment')
            values = load_stage(folder, entry, 'encoded_srgb')
            rows.append({'profile': Path(entry['profile'].replace('\\', '/')).stem,
                         'geometry': entry['geometry'], 'profile_sha256': entry['profile_sha256'],
                         'vs_lr': compare(values, lr), 'vs_baseline': compare(first, values)})
        lab1, y1 = srgb_lab(first)
        lab2, y2 = srgb_lab(lr)
        oracle = lab1.copy()
        oracle[..., 0] = lab2[..., 0]
        de = delta_e_2000(oracle, lab2)
        mask = (y1 > .002) & (y2 > .002) & (first.max(axis=-1) < .999) & (lr.max(axis=-1) < .999)
        result['scenes'].append({'scene': scene, 'lr_metadata': lr_meta, 'crop_candidates': crops,
            'harmonized_warnings': harmonized_warnings(lr_meta['settings']) if lr_suffix else [],
            'new_vs_old_lr': compare(old_lr, lr),
            'lr_xmp_rendering': xmp_render_audit(lr_path),
            'profile_variants': rows, 'oracle_equal_L_delta_e00_mean': float(de.mean()),
            'oracle_equal_L_delta_e00_p95': float(np.percentile(de, 95)),
            'local_y_ratio_ev_p10_p50_p90': np.percentile(np.log2(y2[mask]/y1[mask]), [10, 50, 90]).tolist()})
        if neutral_suffix:
            neutral_path = lr_path.with_name(scene + neutral_suffix + '.tif')
            if not neutral_path.exists():
                result['scenes'][-1]['camera_neutral_reference'] = {'available': False, 'expected_path': str(neutral_path)}
            else:
                entry = next(x for x in entries if 'Camera Neutral.dcp' in x['profile'] and x['geometry'] == geometry)
                neutral = load_stage(folder, entry, 'encoded_srgb')
                neutral_meta = audit_tiff(neutral_path)
                pixels = native_pixels(neutral_path)
                neutral_crops = grid_crop_candidates(pixels, neutral, width, height)
                offset = neutral_crops[0]
                neutral_lr = strict_region(pixels, [0, 0, width, height], 8, offset['top'], offset['left'])
                result['scenes'][-1]['camera_neutral_reference'] = {
                    'available': True, 'lr_metadata': neutral_meta,
                    'harmonized_warnings': harmonized_warnings(neutral_meta['settings'], 'Camera Neutral'),
                    'lr_xmp_rendering': xmp_render_audit(neutral_path),
                    'crop_candidates': neutral_crops, 'ipr_profile_sha256': entry['profile_sha256'],
                    'same_profile_vs_lr': compare(neutral, neutral_lr),
                    'adobe_standard_vs_lr_neutral_same_crop': compare(first, neutral_lr),
                    'lr_adobe_vs_neutral': compare(lr, neutral_lr)}
        pairs.append((first, lr))
        print(scene, 'profile comparisons complete', flush=True)
    for index, (first, lr) in enumerate(pairs):
        training = [pair for n, pair in enumerate(pairs) if n != index]
        row = result['scenes'][index]
        row['tonal_holdout'] = []
        for power in (False, True):
            model = fit_model(training, power)
            rendered, overshoot = apply_model(first, model)
            row['tonal_holdout'].append({'model': 'power' if power else 'exposure', 'parameters': model,
                'held_out_scene': row['scene'], 'training_scene_ids': [r['scene'] for n, r in enumerate(result['scenes']) if n != index],
                'linear_any_over_one_before_clip_fraction': overshoot, 'vs_lr': compare(rendered, lr)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lr-suffix', default='', help='e.g. --lr-suffix=-2; preserve old TIFF references')
    parser.add_argument('--geometry', choices=('with_lcp', 'without_lcp'), default='with_lcp')
    parser.add_argument('--neutral-suffix', help='Optional Camera Neutral LR exports, missing scenes are recorded')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite proof')
    if any(c in suffix for suffix in (args.lr_suffix, args.neutral_suffix or '') for c in '/\\'):
        parser.error('LR suffix must not contain path separators')
    result = analyse(args.probe, args.reference, args.lr_suffix, args.geometry, args.neutral_suffix)
    result['analyser_sha256'] = file_hash(Path(__file__))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)


if __name__ == '__main__':
    main()
