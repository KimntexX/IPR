"""X4 measurements, not parity or a claim of a clean photographic reference."""
import numpy as np

REQUIRED_STAGES = {'unpacked_cfa', 'black_subtracted_cfa', 'normalized_cfa',
                   'pre_demosaic_cfa', 'post_demosaic_camera_rgb', 'pre_output_camera_rgb',
                   'post_median_camera_rgb', 'pre_dcp_camera_rgb'}


def output_effect(before, after, policy):
    x, y = np.asarray(before), np.asarray(after)
    if x.shape != y.shape or not np.isfinite(x).all() or not np.isfinite(y).all():
        raise ValueError('Invalid output-bound samples')
    if policy not in ('parity', 'signed'):
        raise ValueError('Unknown output policy')
    if policy == 'signed' and (x.dtype != y.dtype or x.tobytes() != y.tobytes()):
        raise ValueError('F1 signed output must preserve PreOutput exactly, without bound/grid')
    return dict(before=moments(x), after=moments(y),
                mean_shift=(y.astype(float)-x.astype(float)).mean(axis=(0, 1)).tolist(),
                negative_input_fraction=(x < 0).mean(axis=(0, 1)).tolist(),
                above_one_input_fraction=(x > 1).mean(axis=(0, 1)).tolist(),
                operation='identity_unbounded_float' if policy == 'signed' else 'round_16bit_grid_and_clip_0_1')


def moments(values):
    x = np.asarray(values, dtype=np.float64)
    if x.ndim != 3 or not np.isfinite(x).all() or x.shape[0] * x.shape[1] < 2:
        raise ValueError('Need finite HWC samples, at least two sites')
    flat = x.reshape(-1, x.shape[-1])
    return dict(count=len(flat), mean=flat.mean(axis=0).tolist(),
                variance=flat.var(axis=0, ddof=1).tolist(),
                covariance=np.atleast_2d(np.cov(flat, rowvar=False, ddof=1)).tolist(),
                negative_fraction=(flat < 0).mean(axis=0).tolist(),
                zero_fraction=(flat == 0).mean(axis=0).tolist(),
                at_or_above_one_fraction=(flat >= 1).mean(axis=0).tolist(),
                minimum=flat.min(axis=0).tolist(), maximum=flat.max(axis=0).tolist())


def pair_moments(first, second):
    a, b = np.asarray(first, dtype=np.float64), np.asarray(second, dtype=np.float64)
    if a.shape != b.shape:
        raise ValueError('Pair geometry mismatch')
    delta = (a - b) / np.sqrt(2.)
    stats = moments(delta)
    # Do not silently centre the difference before reporting drift. The centred
    # sample covariance and raw second moment answer different questions.
    stats['raw_second_moment'] = np.mean(delta * delta, axis=(0, 1)).tolist()
    return dict(first=moments(a), second=moments(b), difference_over_sqrt2=stats,
                first_minus_second_mean=(a - b).mean(axis=(0, 1)).tolist(),
                interpretation='Temporal pair difference; common fixed pattern/scene cancels. Motion, illumination drift and registration are not corrected.')


def safe_ratio(numerator, denominator):
    a, b = np.asarray(numerator, dtype=float), np.asarray(denominator, dtype=float)
    if a.shape != b.shape:
        raise ValueError('Ratio geometry mismatch')
    return [float(x / y) if y != 0 else None for x, y in zip(a.flat, b.flat)]


def planes(descriptor, values):
    """CFA phases kept distinct, including the two green sensor planes."""
    if descriptor['layout'] == 'rgb':
        if values.shape[-1] != 3:
            raise ValueError('Expected three camera channels')
        return [('RGB', values)]
    if descriptor['layout'] != 'cfa' or values.shape[-1] != 1:
        raise ValueError('Unsupported layout')
    pattern = descriptor['context']['cfa']
    y0, x0 = descriptor['origin_yx']
    return [(f'phase{dy}{dx}_colour{pattern[(y0 + dy) % 2][(x0 + dx) % 2]}',
             values[dy::2, dx::2]) for dy in range(2) for dx in range(2)]


def measure_pair(captures):
    """Four loaded captures: A/B x two shots, already authenticated by loader."""
    if set(captures) != {'parity', 'signed'} or any(len(v) != 2 for v in captures.values()):
        raise ValueError('Need A and B on the same two shots')
    a, b = captures['parity'], captures['signed']
    if any(v[0]['mode'] != policy for policy, pair in captures.items() for v in pair):
        raise ValueError('Policy/manifest mismatch')
    for shot in range(2):
        if a[shot][0]['source']['sha256'] != b[shot][0]['source']['sha256']:
            raise ValueError('A/B source mismatch')
    keys = set(a[0][1])
    if not keys or any(set(v[1]) != keys for pair in captures.values() for v in pair):
        raise ValueError('Missing stage/ROI')
    if {k[0] for k in keys} != REQUIRED_STAGES:
        raise ValueError('Expected seven I2 stages plus final camera capture')
    origins = {key[1] for key in keys if key[0] == 'unpacked_cfa'}
    if keys != {(stage, origin) for stage in REQUIRED_STAGES for origin in origins}:
        raise ValueError('Incomplete stage/ROI product')
    rows = []
    for key in sorted(keys):
        desc = a[0][1][key][0]
        # Units may differ only by A's documented 16-bit output grid. Both
        # representations are normalized camera values, never integer counts.
        units = {v[1][key][0]['unit'] for pair in captures.values() for v in pair}
        if units != {'DN'} and not units <= {'linear_camera_grid_1_65535', 'linear_camera_white_1'}:
            raise ValueError('Incompatible stage units')
        for shot in range(2):
            da, xa = a[shot][1][key]; db, xb = b[shot][1][key]
            for field in ('shape', 'dtype', 'origin_yx', 'layout', 'orientation', 'context'):
                if da[field] != db[field]:
                    raise ValueError('A/B contract mismatch: ' + field)
            # Calibration may differ between shots, but never between policies.
            for field in ('shape', 'dtype', 'origin_yx', 'layout', 'orientation'):
                if da[field] != desc[field]:
                    raise ValueError('Pair contract mismatch: ' + field)
            if da['context']['cfa'] != desc['context']['cfa']:
                raise ValueError('Pair CFA mismatch')
            if key[0] in ('unpacked_cfa', 'black_subtracted_cfa') and not np.array_equal(xa, xb):
                raise ValueError('A/B input differs before the policy boundary')
        arrays = {policy: [dict(planes(v[1][key][0], v[1][key][1])) for v in pair]
                  for policy, pair in captures.items()}
        for plane in arrays['parity'][0]:
            stats = {policy: pair_moments(pair[0][plane], pair[1][plane])
                     for policy, pair in arrays.items()}
            av = stats['parity']['difference_over_sqrt2']['variance']
            bv = stats['signed']['difference_over_sqrt2']['variance']
            changes = [arrays['signed'][i][plane].astype(float) - arrays['parity'][i][plane].astype(float)
                       for i in range(2)]
            row = dict(stage=key[0], origin_yx=list(key[1]), plane=plane,
                             unit='DN' if units == {'DN'} else 'linear_camera_white_1',
                             policies=stats, signed_over_parity_pair_variance=safe_ratio(bv, av),
                             signed_minus_parity_per_shot=[moments(x) for x in changes])
            if key[0] == 'pre_output_camera_rgb':
                final_key = ('post_median_camera_rgb', key[1])
                row['output_bound_effect'] = {
                    policy: [output_effect(v[1][key][1], v[1][final_key][1], policy) for v in pair]
                    for policy, pair in captures.items()}
            rows.append(row)
    return rows
