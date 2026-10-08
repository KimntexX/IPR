#!/usr/bin/env python3
"""Read-only Adobe Standard cross-check; no fitted or implicit tone curve."""
import argparse
import json
from pathlib import Path

import numpy as np

from analyse_base_colorimetrique_v2 import compare, file_hash
from analyse_canon_white_levels import load_stage
from analyse_color_mixer import native_pixels
from investigate_dcp_curve import encoded
from investigate_dcp_normalization import select_unique
from investigate_dcp_order import PROPHOTO_TO_XYZ, look_table
from investigate_tonal_base import D50_TO_SRGB, local_path, read_dcp


def render(camera, kelvin, tags, baseline=0):
    if (tags.get(50778) != (17,) or tags.get(50779) != (21,) or
            50940 in tags or 51109 in tags or
            any(k not in tags for k in (50964, 50965, 50937, 50938, 50939, 50981, 50982)) or
            tags.get(51107, (0,)) != (0,) or tags.get(51108, (0,)) != (0,)):
        raise ValueError('Unsupported Standard profile')
    weight = np.clip((1/np.clip(kelvin, 1500, 50000)-1/2856)/(1/6500-1/2856), 0, 1)
    forward = ((1-weight)*np.array(tags[50964])+weight*np.array(tags[50965])).reshape(3, 3)
    rgb = ((camera @ forward.T)*2**baseline @ np.linalg.inv(PROPHOTO_TO_XYZ).T).astype(np.float32).astype(float)
    entries = (1-weight)*np.array(tags[50938])+weight*np.array(tags[50939])
    rgb = look_table(rgb, tags[50937], entries, 0).astype(np.float32).astype(float)
    rgb = look_table(rgb, tags[50981], tags[50982], 0).astype(np.float32).astype(float)
    return encoded(rgb @ (D50_TO_SRGB @ PROPHOTO_TO_XYZ).T)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('probe', 'original', 'reference', 'dng-audit', 'profile', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    args = p.parse_args()
    if args.output.exists():
        p.error('Refuse to overwrite evidence')
    paths = [args.probe/'manifest.json', args.original/'manifest.json', args.reference, args.dng_audit, args.profile]
    manifest, original, reference, audit = [json.loads(x.read_text(encoding='utf-8')) for x in paths[:4]]
    tags = read_dcp(args.profile)
    result = {'schema': 1, 'diagnostic_only': True,
              'limitations': 'Restricted Standard renderer with no implicit/default curve; not a full Adobe renderer. Native step-8 crops, no fitting.',
              'analyser_sha256': file_hash(Path(__file__)),
              'inputs_sha256': {str(x): file_hash(x) for x in paths}, 'scenes': []}
    for ref in reference['scenes']:
        scene = ref['scene']
        old = select_unique(manifest, scene, 'baseline')
        fixed = select_unique(manifest, scene, 'specular_fixed')
        dng = next(x for x in audit['scenes'] if x['scene'] == scene)
        cfa = [x for x in dng['sub_ifds'] if x.get('photometric') == 32803]
        if (len(cfa) != 1 or fixed['white_experiment']['candidate_raw_white'] != cfa[0]['white_level'] or
                old['raw_sha256'] != dng['raw_sha256'] or fixed['raw_sha256'] != dng['raw_sha256'] or
                file_hash(local_path(old['raw'])) != dng['raw_sha256'] or file_hash(local_path(dng['path'])) != dng['sha256']):
            raise ValueError('DNG/RAW/white mismatch')
        sources = [x for x in original['images'] if Path(x['raw']).stem == scene and
                   x['geometry'] == 'without_lcp' and x['profile_sha256'] == file_hash(args.profile)]
        if len(sources) != 1 or sources[0]['source_camera_sha256'] != old['stages']['camera_rgb']['sha256']:
            raise ValueError('Standard witness mismatch')
        camera, corrected = [load_stage(args.probe, x, 'camera_rgb') for x in (old, fixed)]
        witness = load_stage(args.original, sources[0], 'encoded_srgb')
        baseline = render(camera, old['wb_kelvin'], tags)
        error = np.abs(baseline-witness)*65535
        if error.max() > 4.000001 or error.mean() > .03:
            raise ValueError(f'Independent renderer differs: {scene}: {error.max()}, {error.mean()}')
        meta = ref['lr_metadata']
        path = local_path(meta['path'])
        if file_hash(path) != meta['sha256']:
            raise ValueError('LR reference changed')
        pixels = native_pixels(path)
        if pixels.dtype != np.uint16:
            raise ValueError('Expected native 16-bit TIFF')
        crop, (h, w) = ref['crop_candidates'][0], camera.shape[:2]
        lr = pixels[crop['top']::8, crop['left']::8, :3][:h,:w].astype(float)/65535
        if lr.shape != camera.shape:
            raise ValueError('Geometry mismatch')
        ev = dng['ifd0']['baseline_exposure']
        variants = {'baseline': baseline, 'white_only': render(corrected, fixed['wb_kelvin'], tags),
                    'base_only': render(camera, old['wb_kelvin'], tags, ev),
                    'white_and_base': render(corrected, fixed['wb_kelvin'], tags, ev)}
        record = {'scene': scene, 'witness_max_code_error': float(error.max()),
                  'witness_mean_code_error': float(error.mean()),
                  'variants': {k: compare(v, lr) for k,v in variants.items()}}
        result['scenes'].append(record)
        print(scene, {k:round(v['delta_e00_mean'],4) for k,v in record['variants'].items()}, flush=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2, allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
