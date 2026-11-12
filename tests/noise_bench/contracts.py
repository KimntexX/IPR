"""Explicit domains: CHW packed CFA or CHW rendered channels, never inferred."""
import numpy as np


def field(values, domain):
    """Validate signed finite data. CFA plane order remains caller metadata."""
    a = np.asarray(values, dtype=np.float64)
    if domain not in ('cfa', 'rendered'):
        raise ValueError('Explicit cfa or rendered domain required')
    if a.ndim != 3 or min(a.shape) < 1 or not np.isfinite(a).all():
        raise ValueError('Nonempty finite CHW field required')
    if domain == 'cfa' and a.shape[0] != 4:
        raise ValueError('Four packed Bayer planes required')
    return a


def mask_for(shape, mask):
    if mask is None:
        return np.ones(shape, dtype=bool)
    m = np.asarray(mask)
    if m.dtype != bool or m.shape != shape or not m.any():
        raise ValueError('Nonempty matching boolean spatial mask required')
    return m
