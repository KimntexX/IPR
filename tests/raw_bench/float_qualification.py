"""F1 pre-DCP signed inlet checks. These do not qualify nonlinear DCP handling."""
import numpy as np
from capture import load_capture


def compare_module_inlet(decoder_capture, module_capture):
    decoder, ds = load_capture(decoder_capture); module, ms = load_capture(module_capture)
    if (decoder['source']['sha256'] != module['source']['sha256'] or
            decoder['engine_revision'] != module['engine_revision'] or
            decoder['mode'] != 'signed' or module['mode'] != 'signed'):
        raise ValueError('Module/decoder source, revision or policy mismatch')
    if module['metadata']['orientation_flip'] != 0:
        return dict(verdict='not_compared_rotated_sparse_ROIs', reason='Sensor/display ROIs are different locations after rotation; no fabricated comparison')
    dk = {origin for stage, origin in ds if stage == 'pre_dcp_camera_rgb'}
    mk = {origin for stage, origin in ms if stage == 'pre_output_camera_rgb'}
    if len(dk) != 3 or dk != mk:
        raise ValueError('Module/decoder inlet ROI geometry mismatch')
    rows = []
    for origin in sorted(dk):
        dd, actual = ds[('pre_dcp_camera_rgb', origin)]
        md, expected = ms[('pre_output_camera_rgb', origin)]
        if any(dd[k] != md[k] for k in ('shape', 'dtype', 'unit', 'layout')):
            raise ValueError('Module/decoder domain mismatch')
        if (not np.isfinite(actual).all() or not np.isfinite(expected).all() or
                actual.tobytes() != expected.tobytes()):
            raise ValueError('Signed module output changed before DCP inlet')
        rows.append(dict(origin_yx=list(origin), bitwise_equal=True, values=int(actual.size)))
    return dict(verdict='pass_exact_module_to_decoder_inlet', orientation_flip=0, rois=rows)


def qualify_inlet(path, require_negatives=False):
    manifest, stages = load_capture(path)
    summary = manifest.get('full_camera')
    if not summary or summary.get('dtype') != 'f32' or summary.get('finite') is not True:
        raise ValueError('Missing finite float32 camera inlet evidence')
    shape = summary.get('shape', [])
    if len(shape) != 3 or shape[-1] != 3 or any(type(v) != int or v <= 0 for v in shape):
        raise ValueError('Invalid inlet geometry')
    if summary.get('unit') != 'linear_camera_white_1' or summary.get('orientation') != 'display':
        raise ValueError('Wrong camera inlet domain')
    negatives = np.asarray(summary.get('negative_fraction'), dtype=float)
    minima = np.asarray(summary.get('minimum'), dtype=float)
    if negatives.shape != (3,) or minima.shape != (3,) or not np.isfinite([negatives, minima]).all():
        raise ValueError('Incomplete inlet statistics')
    if np.any(negatives < 0) or np.any(negatives > 1) or np.any((negatives > 0) != (minima < 0)):
        raise ValueError('Inconsistent signed inlet statistics')
    roi = [(key, desc, values) for key, (desc, values) in stages.items() if key[0] == 'pre_dcp_camera_rgb']
    if len(roi) != 3:
        raise ValueError('Expected three observed inlet ROIs')
    rows = []
    for key, desc, values in roi:
        if (desc['dtype'] != 'f32' or desc['layout'] != 'rgb' or values.shape[-1] != 3 or
                desc['unit'] != summary['unit'] or desc['orientation'] != summary['orientation'] or
                not np.isfinite(values).all()):
            raise ValueError('Invalid inlet ROI contract')
        rows.append(dict(origin_yx=list(key[1]), minimum=values.min(axis=(0, 1)).tolist(),
                         negative_fraction=np.mean(values < 0, axis=(0, 1)).tolist()))
    # Negative presence is required only on black witnesses, never on every
    # normally illuminated photo. Lack of negatives is not a generic failure.
    if require_negatives and (not np.all(negatives > 0) or
                              not all(all(v < 0 for v in row['minimum']) for row in rows)):
        raise ValueError('Signed black witness lost negative values at DCP inlet')
    return dict(verdict='pass_scoped_float_inlet', source=manifest['source'],
                engine_revision=manifest['engine_revision'], full_camera=summary, rois=rows,
                negative_witness_required=require_negatives,
                limitations='Observed DCP inlet only; no proof of fully signed DCP tables/curves, CFA/no-profile inlet or absence of intermediate quantization from negative presence alone.')
