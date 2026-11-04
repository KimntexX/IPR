"""Training-only coherent CFA error normalized by simulated noise variance.

For each physical plane, project error onto the centred clean target and its
curvature component orthogonal in the supplied noise-variance inner product.
Independent zero-mean noise with fixed correct variance has expected normalized
squared projection1. Observed-signal sigma makes this only an approximate scale.
This is a proposed supervision term, not an unbiasedness guarantee for inference.
"""
import torch
from torch.nn import functional as F


def structure_loss(corrected, clean, sigma, border):
    if (corrected.ndim != 4 or corrected.shape[0] == 0 or corrected.shape[1] != 4
            or clean.shape != corrected.shape or sigma.shape != corrected.shape
            or not isinstance(border, int) or isinstance(border, bool) or border < 0
            or min(corrected.shape[-2:]) < 2
            or min(corrected.shape[-2:]) <= 2*border
            or any(v.dtype != torch.float32 or v.device != corrected.device or not torch.isfinite(v).all()
                for v in (corrected, clean, sigma)) or (sigma < 0).any()):
        raise ValueError('Matching finite float32 CFA planes, nonnegative sigma and valid core required')
    crop = (slice(None), slice(None), slice(border, -border or None), slice(border, -border or None))
    target = clean[crop].detach()
    noise = sigma[crop].detach()
    # Subtracting a sample first makes a constant target exactly zero in float32.
    detail = target-target[:, :, :1, :1]
    detail = detail-detail.mean(dim=(-2, -1), keepdim=True)
    variance = (detail.square()*noise.square()).sum(dim=(-2, -1), keepdim=True)
    padded = F.pad(clean.detach(), (1, 1, 1, 1), mode='reflect')
    curvature = ((padded[:, :, 1:-1, :-2]-clean.detach())+(padded[:, :, 1:-1, 2:]-clean.detach())
        +(padded[:, :, :-2, 1:-1]-clean.detach())+(padded[:, :, 2:, 1:-1]-clean.detach()))[crop]
    curvature = curvature-curvature.mean(dim=(-2, -1), keepdim=True)
    curvature_variance = (curvature.square()*noise.square()).sum(dim=(-2, -1), keepdim=True)
    coefficient = (curvature*detail*noise.square()).sum(dim=(-2, -1), keepdim=True)/torch.where(
        variance > 0, variance, torch.ones_like(variance))
    other = curvature-coefficient*detail
    other_variance = (other.square()*noise.square()).sum(dim=(-2, -1), keepdim=True)
    first = variance > 0
    # Relative energy guards reject roundoff on ramps and near-collinear probes.
    second = (curvature_variance > 1e-8*variance) & (other_variance > 1e-4*curvature_variance)
    error = corrected[crop]-target
    terms = []
    for direction, energy, supported in ((detail, variance, first), (other, other_variance, second)):
        numerator = (error*direction).sum(dim=(-2, -1), keepdim=True).square()
        denominator = torch.where(supported, energy, torch.ones_like(energy))
        terms.append(torch.where(supported, numerator/denominator, torch.zeros_like(numerator)))
    count = first.to(corrected.dtype)+second.to(corrected.dtype)
    return ((terms[0]+terms[1])/count.clamp_min(1)).mean()
