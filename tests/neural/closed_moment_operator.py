"""Bench-only transpose-closed interpolation basis; no neural head or noise model."""
import torch
from torch.nn import functional as F

from joint_bayer_unet import validate, bilinear, sparse_rgb
from moment_bayer_unet import STENCILS


def signature(support):
    return tuple(sorted(tuple(tap) for tap in support))


def closed_stencils():
    lookup = {(c, y, x): supports for c, y, x, supports in STENCILS}
    result = []
    for colour, y, x, original in STENCILS:
        supports = list(original)
        seen = {signature(s) for s in supports}
        for support in lookup[colour, x, y]:
            transposed = tuple((dx, dy, weight) for dy, dx, weight in support)
            if signature(transposed) not in seen:
                supports.append(transposed)
                seen.add(signature(transposed))
        result.append((colour, y, x, tuple(supports)))
    return tuple(result)


CLOSED_STENCILS = closed_stencils()
CHANNELS = tuple((c, y, x, support) for c, y, x, supports in CLOSED_STENCILS for support in supports)
CHANNEL_COUNT = len(CHANNELS)


def transpose_permutation():
    lookup = {(c, y, x, signature(s)): i for i, (c, y, x, s) in enumerate(CHANNELS)}
    return tuple(lookup[c, x, y, signature([(dx, dy, weight) for dy, dx, weight in s])]
        for c, y, x, s in CHANNELS)


TRANSPOSE_PERMUTATION = transpose_permutation()


def transpose_coefficients(coefficients):
    if coefficients.ndim != 4 or coefficients.shape[1] != CHANNEL_COUNT:
        raise ValueError('N36HW coefficient tensor required')
    # Permutation is an involution, so inverse gather equals forward mapping.
    return coefficients[:, TRANSPOSE_PERMUTATION].transpose(-2, -1)


def interpolate_closed(packed, pattern, coefficients):
    validate(packed, torch.zeros_like(packed), pattern)
    n, _, h, w = packed.shape
    if (pattern[:, 0] != 0).any() or (pattern[:, 3] != 2).any():
        raise ValueError('Canonical red-TL blue-BR input required')
    if (coefficients.shape != (n, CHANNEL_COUNT, h, w) or coefficients.dtype != packed.dtype
            or coefficients.device != packed.device or not torch.isfinite(coefficients).all()
            or (coefficients.abs() > 1).any()):
        raise ValueError('Finite bounded N36HW coefficients matching input required')
    base = bilinear(packed, pattern)
    observed, _ = sparse_rgb(packed, pattern)
    padded = F.pad(observed, (3, 3, 3, 3), mode='reflect')
    planes = [[base[:, c, y::2, x::2] for y, x in ((0, 0), (0, 1), (1, 0), (1, 1))] for c in range(3)]
    offset = 0
    for colour, y, x, supports in CLOSED_STENCILS:
        centre = planes[colour][2*y+x]
        candidates = []
        for support in supports:
            a, b = [padded[:, colour, 3+dy:3+dy+2*h, 3+dx:3+dx+2*w][:, y::2, x::2]
                for dy, dx, _ in support]
            candidates.append(a*.5+b*.5 if support[0][2] == .5 else a+(a-b)*.5)
        differences = torch.stack(candidates, dim=1)-centre[:, None]
        planes[colour][2*y+x] = centre+(coefficients[:, offset:offset+len(supports)]*differences).sum(dim=1)
        offset += len(supports)
    output = F.pixel_shuffle(torch.stack([p for group in planes for p in group], dim=1), 2)
    if not torch.isfinite(output).all():
        raise ValueError('Interpolation overflow')
    return output
