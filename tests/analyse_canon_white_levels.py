#!/usr/bin/env python3
"""Compare freshly decoded white-level variants; no source/engine writes."""
import argparse
import json
from pathlib import Path

import numpy as np

from analyse_base_colorimetrique_v2 import compare, file_hash, srgb_lab, strict_region
from analyse_color_mixer import native_pixels
from investigate_canon_calibration import clipping


def normalization_gain(old_white, new_white, black):
    if not np.isfinite([old_white, new_white, black]).all() or min(old_white, new_white) <= black:
        raise ValueError('White levels must be finite and above black')
    return (old_white - black) / (new_white - black)


def highlight_metrics(encoded, mask):
    if encoded.shape[:2] != mask.shape or not mask.any():
        raise ValueError('Expected nonempty fixed highlight mask')
    pixels = encoded[mask]
    _, y = srgb_lab(pixels)
    return {'samples': int(mask.sum()), 'mean_y': float(y.mean()),
            'any_channel_high_fraction': float(np.any(pixels >= 65534 / 65535, axis=-1).mean()),
            'all_channels_high_fraction': float(np.all(pixels >= 65534 / 65535, axis=-1).mean())}


def load_stage(folder, item, name):
    entry = item['stages'][name]
    path = folder / item['directory'] / entry['file']
    if file_hash(path) != entry['sha256']:
        raise ValueError('Float hash mismatch: ' + str(path))
    values = np.fromfile(path, dtype='<f4').reshape(entry['rows'], entry['cols'], 3)
    if not np.isfinite(values).all():
        raise ValueError('Nonfinite float grid')
    values = values.astype(np.float64)
    if name == 'encoded_srgb':
        if np.any((values < 0) | (values > 1)):
            raise ValueError('Encoded sRGB outside [0,1]')
        # Restore the original integer codes: float32 transport must not move
        # code 65534 below the clipping threshold or code 1 above the low one.
        values = np.rint(values * 65535) / 65535
    return values


def analyse(folder, reference_path):
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    reference = json.loads(reference_path.read_text(encoding='utf-8'))
    expected = {'baseline', 'full_fixed', 'normal_auto', 'normal_fixed', 'specular_fixed'}
    result = {'schema': 1, 'manifest_sha256': file_hash(folder / 'manifest.json'),
              'reference_sha256': file_hash(reference_path), 'sample_step': manifest['sample_step'],
              'libraw_version': manifest['libraw_version'],
              'method': 'Fixed v3 Lightroom crop, native grids, float32 stored 16-bit sRGB encoding; fixed baseline top-5%-Y mask; candidates not certified',
              'images': []}
    for ref in reference['scenes']:
        scene = ref['scene']
        items = {x['variant']: x for x in manifest['images'] if Path(x['raw']).stem == scene}
        if set(items) != expected:
            raise ValueError('Missing or unexpected variants: ' + scene)
        baseline_item = items['baseline']
        if file_hash(Path(baseline_item['raw'])) != baseline_item['raw_sha256']:
            raise ValueError('RAW hash mismatch')
        first = load_stage(folder, baseline_item, 'encoded_srgb')
        camera_first = load_stage(folder, baseline_item, 'camera_rgb')
        _, first_y = srgb_lab(first)
        highlight_mask = first_y >= np.percentile(first_y, 95)
        samples = {}
        for label in ('ipr', 'lr'):
            meta = ref[label]
            path = Path(meta['path'])
            if file_hash(path) != meta['sha256']:
                raise ValueError('Reference TIFF hash mismatch')
            pixels = native_pixels(path)
            crop = ref['crop_candidates'][0] if label == 'lr' else {'top': 0, 'left': 0}
            width, height = ref['ipr']['size']
            samples[label] = strict_region(pixels, [0, 0, width, height], manifest['sample_step'],
                                           crop['top'], crop['left'])
            del pixels
        if first.shape != samples['ipr'].shape:
            raise ValueError('Probe/export geometry differs')
        rows = []
        for label in ('baseline', 'full_fixed', 'normal_auto', 'normal_fixed', 'specular_fixed'):
            item = items[label]
            for key in ('cam_mul', 'cblack'):
                if item['after_black_estimation'][key] != baseline_item['after_black_estimation'][key]:
                    raise ValueError('WB/black drift between variants')
            if any(item[key] != baseline_item[key] for key in ('dcp', 'lcp', 'wb_kelvin', 'width', 'height', 'raw_sha256')):
                raise ValueError('Profile or geometry drift between variants')
            encoded = load_stage(folder, item, 'encoded_srgb')
            camera = load_stage(folder, item, 'camera_rgb')
            linear = load_stage(folder, item, 'dcp_with_lcp')
            rows.append({'variant': label, 'white_experiment': item['white_experiment'],
                         'before_scale_colors': item['before_scale_colors'],
                         'after_process': item['after_process'],
                         'camera': clipping(camera), 'dcp_linear': clipping(linear),
                         'camera_mean_ratio_to_baseline': (camera.mean(axis=(0, 1)) / camera_first.mean(axis=(0, 1))).tolist(),
                         'vs_baseline': compare(first, encoded),
                         'vs_v3': compare(encoded, samples['ipr']),
                         'vs_lr': compare(encoded, samples['lr']),
                         'fixed_highlights': highlight_metrics(encoded, highlight_mask)})
        result['images'].append({'scene': scene, 'iso': baseline_item['iso'],
            'raw_sha256': baseline_item['raw_sha256'], 'dcp': baseline_item['dcp'], 'lcp': baseline_item['lcp'],
            'independent_record': baseline_item['independent_record'],
            'baseline_max_abs_rgb_difference_from_v3': float(np.abs(first - samples['ipr']).max()),
            'highlight_threshold_y': float(np.percentile(first_y, 95)), 'variants': rows})
        print(scene, 'baseline vs v3 max:', result['images'][-1]['baseline_max_abs_rgb_difference_from_v3'], flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite existing proof')
    result = analyse(args.probe, args.reference)
    result['analyser_sha256'] = file_hash(Path(__file__))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)


if __name__ == '__main__':
    main()
