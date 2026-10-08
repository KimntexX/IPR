#!/usr/bin/env python3
"""Reconcile existing freshly decoded CR3 white variants with audited Adobe DNG.

Read-only inputs. No fitting, no production writes and no proprietary LUT export.
Uses actual specular_fixed decodes, NEVER a scalar rescaling as their substitute.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from analyse_base_colorimetrique_v2 import compare, file_hash, srgb_lab
from analyse_canon_white_levels import load_stage
from analyse_color_mixer import native_pixels
from investigate_dcp_curve import PROPHOTO_TO_SRGB, curve, encoded, hue_stable
from investigate_dcp_order import render, require_profile
from investigate_tonal_base import local_path, read_dcp


def white_scale_gain(old_span, white, black):
    values = np.asarray(black, dtype=float)
    if (values.shape != (4,) or not np.isfinite(values).all() or
            not np.isfinite([old_span, white]).all() or old_span <= 0 or
            not np.all(values == values[0]) or white <= values[0]):
        raise ValueError('Expected a positive span and equal finite CFA black levels')
    return old_span/(white-values[0])


def camera_white_operator(neutral, analog_balance, calibration):
    """DNG reference-neutral normalization, before ForwardMatrix.

    This is applied to un-WB'd camera values. Never apply it to camera_rgb
    already white-balanced by LibRaw without undoing that WB first.
    """
    neutral, analog = np.asarray(neutral, dtype=float), np.asarray(analog_balance, dtype=float)
    matrix = np.asarray(calibration, dtype=float).reshape(3, 3)
    if (neutral.shape != (3,) or analog.shape != (3,) or
            not np.isfinite([*neutral, *analog, *matrix.ravel()]).all() or
            min(neutral) <= 0 or min(analog) <= 0):
        raise ValueError('Invalid neutral or calibration')
    inverse = np.linalg.inv(np.diag(analog) @ matrix)
    reference_neutral = inverse @ neutral
    if np.any(reference_neutral <= 0):
        raise ValueError('Nonpositive reference neutral')
    return np.diag(1/reference_neutral) @ inverse


def select_unique(manifest, scene, variant):
    rows = [x for x in manifest['images'] if Path(x['raw']).stem == scene and x['variant'] == variant]
    if len(rows) != 1:
        raise ValueError('Expected unique camera variant: '+scene+'/'+variant)
    return rows[0]


def source_audit(root):
    """Identifier locations and hashes, not third-party source excerpts."""
    names = ('src/postprocessing/dcraw_process.cpp',
             'src/postprocessing/postprocessing_utils_dcrdefs.cpp',
             'src/postprocessing/postprocessing_utils.cpp',
             'src/postprocessing/mem_image.cpp', 'src/utils/init_close_utils.cpp',
             'src/metadata/tiff.cpp', 'src/metadata/identify.cpp')
    hits = []
    for path in sorted((root/'src').rglob('*.cpp')):
        for number, line in enumerate(path.read_text(encoding='utf-8', errors='replace').splitlines(),1):
            if 'baseline_exposure' in line:
                hits.append({'path':str(path.relative_to(root)), 'line':number})
    return {'source_root':str(root), 'file_hashes':{name:file_hash(root/name) for name in names},
            'baseline_exposure_identifier_hits':hits,
            'postprocessing_baseline_identifier_hits':[x for x in hits if '/postprocessing/' in '/'+x['path']],
            'limitations':'Source inspection of installed vcpkg LibRaw source, not an instrumented runtime Stage3 trace or a proof about every backend.'}


def analyse(args):
    manifest = json.loads((args.probe/'manifest.json').read_text(encoding='utf-8'))
    reference = json.loads(args.reference.read_text(encoding='utf-8'))
    audit = json.loads(args.dng_audit.read_text(encoding='utf-8'))
    previous = json.loads(args.order_measure.read_text(encoding='utf-8'))
    tags = read_dcp(args.profile)
    require_profile(tags)
    result = {'schema': 1, 'diagnostic_only': True,
        'method': 'Same native step-8 crops, restricted independent Neutral renderer; actual specular_fixed LibRaw decodes with white now corroborated by DNG; scalar model is a check only; no fitted gains.',
        'limitations': 'No DNG pixel decode, no SDK Stage3 execution, no physical calibration, no general camera/ISO white-level rule. No full DNG pipeline claim. Demosaic and clipping prevent scalar equivalence.',
        'libraw_version': manifest['libraw_version'], 'analyser_sha256': file_hash(Path(__file__)),
        'dependencies_sha256': {name: file_hash(Path(__file__).with_name(name+'.py')) for name in
            ('investigate_dcp_order', 'investigate_dcp_curve', 'investigate_tonal_base',
             'analyse_canon_white_levels', 'analyse_base_colorimetrique_v2', 'analyse_color_mixer')},
        'inputs_sha256': {str(p): file_hash(p) for p in (args.probe/'manifest.json', args.reference,
            args.dng_audit, args.order_measure, args.profile)}, 'scenes': []}
    if args.libraw_source:
        result['libraw_source_audit'] = source_audit(args.libraw_source)
        result['production_source_hashes'] = {str(p):file_hash(p) for p in
            (Path('src/raw/RawDecoder.cpp'),Path('src/profiles/CameraProfileEngine.cpp'),Path('tests/tonal_base_probe.cpp'))}
    for dng in audit['scenes']:
        scene = dng['scene']
        old = select_unique(manifest, scene, 'baseline')
        fixed = select_unique(manifest, scene, 'specular_fixed')
        if (file_hash(local_path(old['raw'])) != dng['raw_sha256'] or
                old['raw_sha256'] != dng['raw_sha256'] or fixed['raw_sha256'] != dng['raw_sha256'] or
                file_hash(local_path(dng['path'])) != dng['sha256']):
            raise ValueError('Source hash mismatch: '+scene)
        raw_ifds = [x for x in dng['sub_ifds'] if x.get('photometric') == 32803]
        if len(raw_ifds) != 1:
            raise ValueError('Expected unique CFA DNG SubIFD')
        raw_ifd, root = raw_ifds[0], dng['ifd0']
        white, black = raw_ifd['white_level'], raw_ifd['black_level']
        if (fixed['white_experiment']['candidate_raw_white'] != white or
                not fixed['white_experiment']['disable_auto_maximum'] or
                fixed['after_process']['maximum'] != white-black[0] or
                old['after_black_estimation']['cblack'] != black or
                fixed['after_black_estimation']['cblack'] != black or
                old['wb_kelvin'] != fixed['wb_kelvin'] or
                old['after_process']['pre_mul'] != fixed['after_process']['pre_mul'] or
                old['after_process']['libraw_dng_baseline_exposure'] != -999):
            raise ValueError('Normalization/WB contract mismatch: '+scene)
        gain = white_scale_gain(old['after_process']['maximum'], white, black)
        camera, corrected = load_stage(args.probe, old, 'camera_rgb'), load_stage(args.probe, fixed, 'camera_rgb')
        if camera.shape != corrected.shape:
            raise ValueError('Camera geometry mismatch')
        scale_error = np.abs(corrected-np.clip(camera*gain, 0, 1))*65535
        neutral = np.array(root['as_shot_neutral'])
        neutral_gains = (1/neutral) / (1/neutral)[1]
        libraw_gains = np.array(old['after_process']['pre_mul'][:3])
        inverse_camera = np.linalg.inv(np.diag(root['analog_balance']) @ np.array(root['camera_calibration_1']).reshape(3,3))
        operator = camera_white_operator(neutral, root['analog_balance'], root['camera_calibration_1'])
        # Translates values that have ALREADY received LibRaw WB into the
        # reference-neutral convention; do not apply the WB a second time.
        residual_operator = operator @ np.diag(1/libraw_gains)
        record = {'scene': scene, 'raw_sha256': dng['raw_sha256'], 'dng_sha256': dng['sha256'],
            'white_dng_equals_canon_specular_candidate': True,
            'iso': old['iso'], 'dng_white': white, 'black_rgbg': black,
            'old_scale_span': old['after_process']['maximum'], 'new_scale_span': fixed['after_process']['maximum'],
            'white_scale_gain': gain, 'white_scale_ev': float(np.log2(gain)),
            'dng_baseline_exposure_ev': root['baseline_exposure'],
            'camera_baseline_plus_white_ev': float(root['baseline_exposure']+np.log2(gain)),
            'wb_libraw_rgb': libraw_gains.tolist(), 'wb_dng_rgb': neutral_gains.tolist(),
            'wb_max_relative_error': float(np.max(np.abs(neutral_gains/libraw_gains-1))),
            'calibration_1_equals_2': root['camera_calibration_1'] == root['camera_calibration_2'],
            'reference_neutral': (inverse_camera @ neutral).tolist(),
            'camera_wb_residual_operator': residual_operator.tolist(),
            'camera_wb_residual_max_abs_identity': float(np.max(np.abs(residual_operator-np.eye(3)))),
            'actual_vs_scalar_camera_mae_16bit_codes': float(scale_error.mean()),
            'actual_vs_scalar_camera_p95_16bit_codes': float(np.percentile(scale_error,95)),
            'camera_any_high_fraction_old': float(np.any(camera >= 65534/65535, axis=-1).mean()),
            'camera_any_high_fraction_new': float(np.any(corrected >= 65534/65535, axis=-1).mean()),
            'camera_grid_hashes': {variant: item['stages']['camera_rgb']['sha256'] for variant,item in
                (('baseline',old),('specular_fixed',fixed))}}
        ref = next(x for x in reference['scenes'] if x['scene'] == scene)['camera_neutral_reference']
        if not ref['available']:
            record['comparison_available'] = False
            result['scenes'].append(record)
            continue
        previous_row = next(x for x in previous['scenes'] if x['scene'] == scene)
        if previous_row['camera_sha256'] != record['camera_grid_hashes']['baseline']:
            raise ValueError('Previous camera witness mismatch')
        path = local_path(ref['lr_metadata']['path'])
        if file_hash(path) != ref['lr_metadata']['sha256']:
            raise ValueError('Reference changed')
        pixels = native_pixels(path)
        h,w = camera.shape[:2]
        crop = ref['crop_candidates'][0]
        if pixels.dtype != np.uint16:
            raise ValueError('Expected 16-bit TIFF')
        lr = pixels[crop['top']::8,crop['left']::8,:3][:h,:w].astype(float)/65535
        if lr.shape != camera.shape:
            raise ValueError('Reference geometry mismatch')
        _, baseline = render(camera, old['wb_kelvin'], tags, root['baseline_exposure'])
        _, new = render(corrected, fixed['wb_kelvin'], tags, root['baseline_exposure'])
        _, wb_corrected = render(corrected @ residual_operator.T, fixed['wb_kelvin'], tags, root['baseline_exposure'])
        variants = {'baseline_witness': encoded(baseline['production_order_reconstructed']),
            'base_exposure_only': encoded(baseline['dng_baseline_plus_profile_offset']),
            'white_only_fresh_decode': encoded(new['production_order_reconstructed']),
            'white_and_base_exposure': encoded(new['dng_baseline_plus_profile_offset']),
            'white_base_and_reference_wb': encoded(wb_corrected['dng_baseline_plus_profile_offset'])}
        # Obtain the same pre-curve table output with baseline gain folded into
        # camera values. Verify that roundoff does not change the witness.
        pre_curve_srgb, _ = render(corrected*2**root['baseline_exposure'], fixed['wb_kelvin'], tags)
        pre_curve = pre_curve_srgb @ np.linalg.inv(PROPHOTO_TO_SRGB).T
        curve_witness = encoded(curve(pre_curve,tags[50940]) @ PROPHOTO_TO_SRGB.T)
        errors = np.abs(curve_witness-variants['white_and_base_exposure'])*65535
        if errors.max() > 1.000001 or errors.mean() > .01:
            raise ValueError('Normalized pre-curve witness mismatch')
        record['normalized_curve_witness_max_code_error'] = float(errors.max())
        record['normalized_curve_witness_mean_code_error'] = float(errors.mean())
        variants['white_base_hue_stable_linear'] = encoded(hue_stable(pre_curve,tags[50940]) @ PROPHOTO_TO_SRGB.T)
        variants['white_base_hue_stable_cubic'] = encoded(hue_stable(pre_curve,tags[50940],True) @ PROPHOTO_TO_SRGB.T)
        if abs(compare(variants['baseline_witness'], lr)['delta_e00_mean']-
               previous_row['variants']['production_order_reconstructed']['vs_lr']['delta_e00_mean']) > 1e-10:
            raise ValueError('Historical baseline mismatch')
        y = srgb_lab(lr)[1]
        masks = {'lr_dark_y_le_002': y<=.02, 'lr_highlight_top5': y>=np.percentile(y,95)}
        record.update(comparison_available=True, lr_sha256=ref['lr_metadata']['sha256'],
            variants={k: {'vs_lr':compare(v,lr), 'regions':{key:compare(v[mask],lr[mask])
                      for key,mask in masks.items() if mask.any()}} for k,v in variants.items()})
        result['scenes'].append(record)
        print(scene,'DE00:', {k:round(v['vs_lr']['delta_e00_mean'],4) for k,v in record['variants'].items()},flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('probe','reference','dng-audit','order-measure','profile','output'):
        parser.add_argument('--'+option,type=Path,required=True)
    parser.add_argument('--libraw-source',type=Path)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Refuse to overwrite evidence')
    result = analyse(args)
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)
        stream.write('\n')


if __name__ == '__main__':
    main()
