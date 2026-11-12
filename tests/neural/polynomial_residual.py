"""Observed residual from a local least-squares quadratic centre estimate."""
import torch
from torch.nn import functional as F


# Isotropic5x5 quadratic fit: w(x,y)=(27-5*(x*x+y*y))/175.
# Centre weight27/175. D4 orbit sums keep affine/quadratic moments exact
# algebraically and avoid subtracting a large common brightness level.
ORBITS = (
    (22/175, (((0, -1), (0, 1)), ((-1, 0), (1, 0)))),
    (17/175, (((-1, -1), (1, 1)), ((-1, 1), (1, -1)))),
    (7/175, (((0, -2), (0, 2)), ((-2, 0), (2, 0)))),
    (2/175, (((-2, -1), (2, 1)), ((-1, -2), (1, 2)),
        ((-2, 1), (2, -1)), ((-1, 2), (1, -2)))),
    (-13/175, (((-2, -2), (2, 2)), ((-2, 2), (2, -2)))),
)


def quadratic_kernel():
    y, x = torch.meshgrid(torch.arange(-2, 3, dtype=torch.float64), torch.arange(-2, 3, dtype=torch.float64), indexing='ij')
    return (27-5*(x.square()+y.square()))/175


def residual_and_variance(packed, sigma):
    """Validated N4HW inputs expected; noise samples assumed independent.

    Variance includes covariance of centre with its fitted value through the
    residual centre coefficient148/175. Inputs remain unchanged and signed.
    """
    h, w = packed.shape[-2:]
    values = F.pad(packed, (2, 2, 2, 2), mode='reflect')
    variance = F.pad(sigma.square(), (2, 2, 2, 2), mode='reflect')
    residual = torch.zeros_like(packed)
    total_variance = sigma.square()*(148/175)**2
    for weight, pairs in ORBITS:
        differences, noise = [], []
        for pair in pairs:
            a, b = [values[:, :, 2+y:2+y+h, 2+x:2+x+w] for y, x in pair]
            differences.append((packed-a)+(packed-b))
            a, b = [variance[:, :, 2+y:2+y+h, 2+x:2+x+w] for y, x in pair]
            noise.append(a+b)
        contrast, spread = differences[0]+differences[1], noise[0]+noise[1]
        if len(pairs) == 4:
            contrast = contrast+(differences[2]+differences[3])
            spread = spread+(noise[2]+noise[3])
        residual = residual+weight*contrast
        total_variance = total_variance+weight**2*spread
    return residual, total_variance
