"""Capture-level dark repeatability, complementing neural dark_bias_probe.

No training split, camera naming convention or inferred dark scene. Supply
two independently acquired closed-lens sets at the same explicit ISO.
"""
import numpy as np
from .contracts import field, mask_for


def black_repeatability(first, second, first_ids, second_ids, *, iso,
                        black_dn, capture_kind, mask=None):
    """Native DN NCHW sets; report pedestal and centered temporal variation.

    Each set needs >=2 captures. IDs must identify source captures, not crops.
    Disjoint IDs prevent reuse but do not prove statistical independence.
    """
    if capture_kind != 'closed_lens':
        raise ValueError('Explicit closed_lens acquisition required, not a dark scene')
    if isinstance(iso, bool) or not isinstance(iso, (int, np.integer)) or iso <= 0:
        raise ValueError('Positive ISO required; do not mix ISO in a set')
    arrays = [np.asarray(a, dtype=float) for a in (first, second)]
    identities = [list(first_ids), list(second_ids)]
    for a, ids in zip(arrays, identities):
        if a.ndim != 4 or len(a) < 2 or len(ids) != len(a):
            raise ValueError('Two or more identified NCHW dark captures per set required')
        for frame in a:
            field(frame, 'cfa')
    all_ids = identities[0]+identities[1]
    if (any(not isinstance(i, str) or not i for i in all_ids)
            or len(set(all_ids)) != len(all_ids)):
        raise ValueError('Distinct source capture identities across both sets required')
    if arrays[0].shape[1:] != arrays[1].shape[1:]:
        raise ValueError('Matching packed CFA support required')
    black = np.asarray(black_dn, dtype=float)
    if black.shape != (4,) or not np.isfinite(black).all():
        raise ValueError('Four explicit finite black levels in DN required')
    m = mask_for(arrays[0].shape[2:], mask)
    rows = []
    for a in arrays:
        values = a[:, :, m]
        means = values.mean(axis=2)
        # Remove the per-capture pedestal before estimating temporal noise.
        centered = values-means[:, :, None]
        rows.append(dict(captures=len(a), frame_mean_dn=means.tolist(),
                         mean_bias_dn=(means.mean(axis=0)-black).tolist(),
                         frame_mean_std_dn=means.std(axis=0, ddof=1).tolist(),
                         centered_temporal_variance_dn2=centered.var(axis=0, ddof=1).mean(axis=1).tolist()))
    delta = np.mean(arrays[1][:, :, m], axis=(0, 2))-np.mean(arrays[0][:, :, m], axis=(0, 2))
    return dict(domain='cfa', iso=int(iso), unit='DN', valid_pixels=int(m.sum()),
                sets=rows, second_minus_first_pedestal_dn=delta.tolist(),
                independence_verified=False)
