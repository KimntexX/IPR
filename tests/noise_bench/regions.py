"""ROI support and descriptive content; no automatic texture/noise verdict."""
import numpy as np
from .contracts import field, mask_for


def region_support(shape_hw, rect_xywh, *, halo):
    """Full rectangular context required; no clipping or implicit model margin."""
    if len(shape_hw) != 2 or len(rect_xywh) != 4:
        return False
    values = (*shape_hw, *rect_xywh, halo)
    if any(isinstance(v, bool) or not isinstance(v, (int, np.integer)) for v in values):
        return False
    height, width = shape_hw
    x, y, w, h = rect_xywh
    return (height > 0 and width > 0 and halo >= 0 and w > 0 and h > 0
            and x >= halo and y >= halo and x+w+halo <= width and y+h+halo <= height)


def describe_region(image, rect_xywh, *, domain, halo, mask=None):
    """Mask must cover the whole core and halo, not only the sampled center.

    Content gradients use adjacent valid samples in the core; halo is a
    caller-declared processing context, not a fixed U-Net receptive field.
    Gradients include noise. Flat regions can be useful for noise measurements.
    """
    a = field(image, domain)
    if not region_support(a.shape[1:], rect_xywh, halo=halo):
        raise ValueError('Full in-bounds ROI and explicit halo required')
    m = mask_for(a.shape[1:], mask)
    x, y, w, h = rect_xywh
    support = m[y-halo:y+h+halo, x-halo:x+w+halo]
    if not support.all():
        return dict(domain=domain, eligible=False, reason='invalid_context_mask',
                    valid_context_fraction=float(support.mean()))
    core = a[:, y:y+h, x:x+w]
    dx, dy = np.diff(core, axis=2), np.diff(core, axis=1)
    # No invented derivative on a one-pixel axis.
    energy = ((np.mean(dx**2, axis=(1, 2)) if w > 1 else np.zeros(a.shape[0]))
              +(np.mean(dy**2, axis=(1, 2)) if h > 1 else np.zeros(a.shape[0])))
    return dict(domain=domain, eligible=True, reason='support_only_content_needs_review',
                rect_xywh=list(rect_xywh), halo=int(halo),
                mean=core.mean(axis=(1, 2)).tolist(),
                span=np.ptp(core, axis=(1, 2)).tolist(),
                constant_by_plane=(np.ptp(core, axis=(1, 2)) == 0).tolist(),
                gradient_energy=energy.tolist(),
                gradient_sample_counts_xy=[h*max(w-1, 0), w*max(h-1, 0)],
                texture_noise_separation_verified=False)
