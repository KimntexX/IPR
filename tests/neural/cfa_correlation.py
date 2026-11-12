"""Observation-only central-difference correlations, not a calibrated detector."""
import torch
from torch.nn import functional as F

WINDOWS = (9, 17)
PACKED_RADIUS = 2+max(WINDOWS)//2


def correlation_features(values, sigma, window):
    if (values.ndim != 4 or values.shape[0] == 0 or values.shape[1] != 4 or sigma.shape != values.shape
            or values.dtype != torch.float32 or sigma.dtype != values.dtype or sigma.device != values.device
            or isinstance(window, bool) or not isinstance(window, int) or window < 3 or window % 2 != 1
            or min(values.shape[-2:]) <= 2
            or not torch.isfinite(values).all() or not torch.isfinite(sigma).all() or (sigma < 0).any()):
        raise ValueError('Finite float32 CFA/sigma and supported odd correlation window required')
    if (sigma == 0).all():
        return values.new_zeros(len(values), 4, 4, *values.shape[-2:])
    height, width = values.shape[-2:]
    padded = F.pad(values, (2, 2, 2, 2), mode='reflect')
    centre = padded[:, :, 2:2+height, 2:2+width]
    left, right = padded[:, :, 2:2+height, 1:1+width], padded[:, :, 2:2+height, 3:3+width]
    up, down = padded[:, :, 1:1+height, 2:2+width], padded[:, :, 3:3+height, 2:2+width]
    horizontal = right-left
    vertical = down-up
    hleft = centre-padded[:, :, 2:2+height, :width]
    hright = padded[:, :, 2:2+height, 4:4+width]-centre
    vup = centre-padded[:, :, :height, 2:2+width]
    vdown = padded[:, :, 4:4+height, 2:2+width]-centre
    # Lag1 uses disjoint noise samples. Lag2 shares the centre with opposite signs.
    raw = torch.stack((.5*horizontal*(hleft+hright), hleft*hright+sigma.square(),
        .5*vertical*(vup+vdown), vup*vdown+sigma.square()), dim=2)
    variance = F.pad(sigma.square(), (1, 1, 1, 1), mode='reflect')
    hvar = variance[:, :, 1:1+height, :width]+variance[:, :, 1:1+height, 2:2+width]
    vvar = variance[:, :, :height, 1:1+width]+variance[:, :, 2:2+height, 1:1+width]
    normalizer = torch.stack((hvar, hvar, vvar, vvar), dim=2)
    radius = window//2
    def smooth(array):
        rows = F.avg_pool2d(F.pad(array.reshape(len(values), 16, height, width),
            (radius, radius, 0, 0), mode='replicate'), (1, window), stride=1)
        return F.avg_pool2d(F.pad(rows, (0, 0, radius, radius), mode='replicate'),
            (window, 1), stride=1)
    numerator, denominator = smooth(raw), smooth(normalizer)
    result = torch.where(denominator > 0, numerator/denominator.clamp_min(1e-24), torch.zeros_like(numerator))
    if not torch.isfinite(result).all():
        raise ValueError('Correlation feature overflow')
    return result.reshape(len(values), 4, 4, height, width)
