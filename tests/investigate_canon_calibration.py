#!/usr/bin/env python3
"""Summarize independently read Canon fields, optical borders and float clipping.

No engine writes. No optical registration or assumption that Lightroom is truth.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from analyse_base_colorimetrique_v2 import file_hash


def clipping(rgb):
    rgb = np.asarray(rgb, dtype=np.float64)
    if rgb.ndim != 3 or rgb.shape[2] != 3 or not np.isfinite(rgb).all():
        raise ValueError('Expected finite RGB float image')
    return {'min_rgb': rgb.min(axis=(0, 1)).tolist(),
            'max_rgb': rgb.max(axis=(0, 1)).tolist(),
            'mean_rgb': rgb.mean(axis=(0, 1)).tolist(),
            'any_negative_fraction': float(np.any(rgb < 0, axis=-1).mean()),
            'any_over_one_fraction': float(np.any(rgb > 1, axis=-1).mean()),
            'any_channel_rounds_to_zero_linear16_fraction': float(np.any(rgb < .5 / 65535., axis=-1).mean()),
            'all_nonpositive_fraction': float(np.all(rgb <= 0, axis=-1).mean()),
            'all_opaque_black_after_clip_fraction': float(np.all(np.clip(rgb, 0, 1) == 0, axis=-1).mean())}


def analyse(folder, independent_path):
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    independent = json.loads(independent_path.read_text(encoding='utf-8'))
    by_scene = {Path(x['raw']).stem: x for x in independent['images']}
    result = {'schema': 1, 'probe_manifest_sha256': file_hash(folder / 'manifest.json'),
              'independent_metadata_sha256': file_hash(independent_path),
              'libraw_version': manifest['libraw_version'], 'sample_step': manifest['sample_step'],
              'limitations': 'No RAW modifications; stage grids precede editor orientation; clipping fractions describe sampled floats, not all exported pixels. Linear16 zero prediction assumes clamp then rounding before sRGB transfer as in toLinearRgba64. ColorData candidates are not a complete Canon specification.',
              'images': []}
    for item in manifest['images']:
        scene = Path(item['raw']).stem
        independent_item = by_scene[scene]
        if file_hash(Path(item['raw'])) != independent_item['raw_sha256']:
            raise ValueError('RAW hash mismatch: ' + scene)
        if len(independent_item['color_data_records']) != 1:
            raise ValueError('Ambiguous ColorData records: ' + scene)
        fields = independent_item['color_data_records'][0]
        words = fields['candidate_words_not_certified']
        wb = words['0x69']
        stage_metrics = {}
        for name, stage in item['stages'].items():
            path = folder / stage['file']
            if file_hash(path) != stage['sha256']:
                raise ValueError('Float stage hash mismatch')
            rgb = np.fromfile(path, dtype='<f4').reshape(stage['rows'], stage['cols'], 3)
            stage_metrics[name] = clipping(rgb)
        border = item['border_audit']
        medians = {name: [c.get('median') for c in value['channels_rgbg']]
                   for name, value in border.items() if isinstance(value, dict) and 'channels_rgbg' in value}
        result['images'].append({'scene': scene, 'raw_sha256': independent_item['raw_sha256'],
            'canon_color_data': fields, 'before_black_estimation': item['before_black_estimation'],
            'after_black_estimation': item['after_black_estimation'], 'after_process': item['after_process'],
            'border_audit': border, 'border_medians_rgbg': medians,
            'as_shot_candidate_rgbg': [wb[0], wb[1], wb[3], wb[2]],
            'as_shot_candidate_kelvin': wb[4],
            'wb_gains_match_libraw': [wb[0], wb[1], wb[3], wb[2]] == item['before_black_estimation']['cam_mul'],
            'estimated_kelvin': item['wb_kelvin'], 'stage_clipping': stage_metrics})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite an existing measurement')
    result = analyse(args.probe, args.metadata)
    result['analyser_sha256'] = file_hash(Path(__file__))
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
    for item in result['images']:
        print(item['scene'], 'WB matches:', item['wb_gains_match_libraw'],
              'borders:', item['border_medians_rgbg'],
              'DCP clipping:', item['stage_clipping']['dcp_with_lcp'])


if __name__ == '__main__':
    main()
