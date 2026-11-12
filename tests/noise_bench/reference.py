"""Reference audits complement existing neural preparation and scoring tools."""
import numpy as np
from .contracts import field, mask_for


def crop_transform(reference_to_source, reference_to_target, origin_xy):
    """Target-local destination -> source-local sampling, without resampling."""
    matrices = []
    for value in (reference_to_source, reference_to_target):
        value = np.asarray(value, dtype=float)
        if value.shape != (2, 3) or not np.isfinite(value).all():
            raise ValueError('Finite 2x3 affine matrices required')
        matrix = np.eye(3)
        matrix[:2] = value
        if abs(np.linalg.det(matrix)) < np.finfo(float).eps:
            raise ValueError('Invertible affine matrices required')
        matrices.append(matrix)
    origin = np.asarray(origin_xy, dtype=float)
    if origin.shape != (2,) or not np.isfinite(origin).all():
        raise ValueError('Finite xy origin required')
    local = np.eye(3)
    local[:2, 2] = origin
    return (np.linalg.inv(local) @ matrices[0] @ np.linalg.inv(matrices[1]) @ local)[:2]


def paired_risk(output, first, second, *, domain, mask=None):
    """Per-plane product risk, in squared input units; negative values retained.

    Unbiased only if both references have independent zero-mean noise, match
    the same scene, and neither reference noise is used to construct output.
    Common noise, motion and illumination mismatch violate those assumptions.
    """
    arrays = [field(a, domain) for a in (output, first, second)]
    if any(a.shape != arrays[0].shape for a in arrays):
        raise ValueError('Matching fields required')
    m = mask_for(arrays[0].shape[1:], mask)
    return np.mean(((arrays[0]-arrays[1])*(arrays[0]-arrays[2]))[:, m], axis=1)


def audit_reference(frames, capture_ids, *, domain, mask=None):
    """Describe an already geometrically audited stack; does not align or accept it.

    Temporal variance and half differences include motion/light drift. Common
    fixed-pattern noise is invisible. Independence cannot be certified here.
    """
    a = np.asarray(frames, dtype=float)
    ids = list(capture_ids)
    if (a.ndim != 4 or len(a) < 4 or len(ids) != len(a)
            or any(not isinstance(i, str) or not i for i in ids)
            or len(set(ids)) != len(ids)):
        raise ValueError('At least four distinct captures in NCHW required')
    for image in a:
        field(image, domain)
    m = mask_for(a.shape[2:], mask)
    values = a[:, :, m]
    means = values.mean(axis=2)
    variance = values.var(axis=0, ddof=1).mean(axis=1)
    half = values[::2].mean(axis=0)-values[1::2].mean(axis=0)
    return dict(domain=domain, capture_count=len(a), valid_pixels=int(m.sum()),
                frame_mean_by_plane=means.tolist(),
                frame_mean_range_by_plane=np.ptp(means, axis=0).tolist(),
                temporal_variance_by_plane=variance.tolist(),
                mean_variance_if_independent_by_plane=(variance/len(a)).tolist(),
                half_counts=[len(a[::2]), len(a[1::2])],
                half_difference_mse_by_plane=np.mean(half**2, axis=1).tolist(),
                independence_verified=False, geometry_verified=False)
