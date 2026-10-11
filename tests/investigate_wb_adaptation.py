"""Diagnostic ablation of post-profile WB, never a production render or fit.

Reads the operator-selected shared CameraNeutral/AsShot neutrals and existing
LR variants. Already-encoded TIFFs cannot reconstruct the pre-DCP camera RGB,
HDR headroom or tone curves; these projections test a hypothesis only.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path

import numpy as np

from analysis import groups, native

RGB_XYZ = np.array([[.4124564, .3575761, .1804375],
                    [.2126729, .7151522, .0721750],
                    [.0193339, .1191920, .9503041]])
BRADFORD = np.array([[.8951, .2664, -.1614], [-.7502, 1.7135, .0367],
                     [.0389, -.0685, 1.0296]])
Y_WEIGHTS = np.array([.2126, .7152, .0722])


def white_lms(t):
    u = (.860117757 + 1.54118254e-4*t + 1.28641212e-7*t*t) / (
        1 + 8.42420235e-4*t + 7.08145163e-7*t*t)
    v = (.317398726 + 4.22806245e-5*t + 4.20481691e-8*t*t) / (
        1 - 2.89741816e-5*t + 1.61456053e-7*t*t)
    x, y = 3*u / (2*u - 8*v + 4), 2*v / (2*u - 8*v + 4)
    return BRADFORD @ np.array([x/y, 1., (1-x-y)/y])


def adaptation(temperature, reference, model):
    gains = white_lms(reference) / white_lms(temperature)
    if model in ('current_projection', 'without_cold_diagonal'):
        gains[1] = 1.
    elif model != 'full_Bradford':
        raise ValueError('Unknown hypothesis')
    result = np.linalg.inv(RGB_XYZ) @ np.linalg.inv(BRADFORD) @ np.diag(gains) @ BRADFORD @ RGB_XYZ
    if model == 'current_projection' and temperature < reference and reference > 2000:
        strength = np.clip(np.log(reference/temperature) / np.log(reference/2000), 0, 1)
        result = np.diag([1-.65*strength, 1+.19*strength, 1-.16*strength]) @ result
    return result


def encode_gamut(rgb):
    rgb = np.maximum(rgb, 0.)
    y = rgb @ Y_WEIGHTS
    rgb = rgb / np.maximum(y, 1.)[..., None]
    y = np.minimum(y, 1.)
    chroma = rgb - y[..., None]
    positive = np.where(chroma > 0, (1-y[..., None])/np.maximum(chroma, 1e-20), 1.)
    negative = np.where(chroma < 0, y[..., None]/np.maximum(-chroma, 1e-20), 1.)
    scale = np.clip(np.minimum(positive, negative).min(axis=-1), 0, 1)
    result = np.clip(y[..., None] + chroma * scale[..., None], 0, 1)
    encoded = np.where(result <= .0031308, 12.92*result,
                       1.055*result**(1/2.4)-.055)
    return np.rint(encoded*65535) / 65535


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit-memory', type=Path, required=True)
    parser.add_argument('--temperature-memory', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=Path('docs/validation/raw'))
    args = parser.parse_args()
    source_hash = native.sha256(__file__)
    input_hashes = {str(p.resolve()): native.sha256(p) for p in (args.audit_memory, args.temperature_memory)}
    audit, original = native.read_json(args.audit_memory), native.read_json(args.temperature_memory)
    references = {r['scene']: r['provenance']['reference_temperature_kelvin'] for r in original['records']
                  if r['engine'] == 'IPR' and r['feature'] == 'temperature'}
    checked, result = [], []
    def load(path, expected=None):
        path = native.local_path(path)
        pixels, metadata, before = native.inspect(path)
        if expected and metadata['sha256'] != expected:
            raise ValueError('Pixel hash differs from audit memory: ' + str(path))
        checked.append((path, before, metadata['sha256']))
        return native.encoded(pixels, step=8), metadata
    for scene in audit['audit']['scenes']:
        chosen = {item['engine']: item for item in scene['shared_neutrals'] if item['selected_reference']}
        if set(chosen) != {'IPR', 'LR'}:
            continue
        ipr, im = load(chosen['IPR']['metadata']['path'], chosen['IPR']['metadata']['sha256'])
        lr, lm = load(chosen['LR']['metadata']['path'], chosen['LR']['metadata']['sha256'])
        for variant in scene['variants']:
            if variant['feature'] != 'temperature' or variant['value'] not in (2000, 3500, 9000):
                continue
            changed, vm = load(variant['file'], variant['sha256'])
            setting = {'feature': 'temperature', 'value': variant['value']}
            measured = groups.measure('couleur', setting, lr, changed)
            hypotheses = []
            for model in ('current_projection', 'without_cold_diagonal', 'full_Bradford'):
                matrix = adaptation(variant['value'], references[scene['scene']], model)
                projected = encode_gamut(native.linearize(ipr) @ matrix.T)
                stats = groups.measure('couleur', setting, ipr, projected)
                delta = stats['delta_log_R_B']
                hypotheses.append({'model': model, 'matrix': matrix.tolist(), 'metrics': stats,
                                   'axis_difference_to_LR': None if delta is None or measured['delta_log_R_B'] is None
                                   else delta - measured['delta_log_R_B']})
            result.append({'scene': scene['scene'], 'temperature': variant['value'], 'status': 'diagnostic',
                           'IPR_reference_metadata': im, 'LR_reference_metadata': lm, 'LR_variant_metadata': vm,
                           'reference_kelvin_from_prior_decode': references[scene['scene']],
                           'reference_kelvin_is_not_measured_from_selected_TIFF': True,
                           'LR_metrics': measured, 'hypotheses': hypotheses,
                           'reference_mismatches': variant['shared_neutral_mismatches']})
        print('WB hypotheses:', scene['scene'], flush=True)
    for path, before, digest in checked:
        native.verify_unchanged(path, before, digest)
    if native.sha256(__file__) != source_hash or any(native.sha256(p) != h for p, h in input_hashes.items()):
        raise ValueError('Investigation source or input memory changed; no publication')
    now = datetime.now(timezone.utc)
    document = {'schema_version': 1, 'data_version': 2, 'group': 'couleur', 'measured_at_utc': now.isoformat(),
                'script_sha256': source_hash, 'input_memories_sha256': input_hashes,
                'method': {'global_step': 8, 'space': 'linear sRGB D65', 'pixel_resampling': False,
                           'tint_delta_applied_to_AsShot_neutral': 0, 'gamut': 'production formula projection',
                           'models_are_not_production_renders': True},
                'units': {'temperature': 'kelvin', 'axis': 'natural log ratio', 'percent': 'percent of sampled pixels',
                          'Y': 'relative linear luminance', 'matrix': 'dimensionless'},
                'records': result, 'cleanup_eligible': False,
                'limits': ['Encoded neutral TIFF has lost pre-DCP camera data and out-of-gamut headroom',
                           'Post-profile projection is not a test of white balance before DCP',
                           'Source CCT is reused from prior RAW decoding, not inferred from neutral filenames',
                           'Operator selected CameraNeutral-AsShot IPR neutrals and confirmed RAW normalization off',
                           'LR and IPR masks are independent; no cross-engine pixel MAE',
                           'No coefficient fit, no production modification or automatic acceptance']}
    output = args.output_dir / ('investigation-adaptation-wb-v002-' + now.strftime('%Y%m%dT%H%M%S%fZ') + '.json')
    native.write_immutable(output, document)
    print(output, flush=True)


if __name__ == '__main__':
    main()
