"""Training-only coherent CFA error normalized by simulated noise variance.

For each physical plane, project the error onto the centred clean target.
Independent zero-mean noise with fixed correct variance has expected normalized
squared projection1. Observed-signal sigma makes this only an approximate scale.
This is a proposed supervision term, not an unbiasedness guarantee for inference.
"""
import torch


def structure_loss(corrected, clean, sigma, border):
    if (corrected.ndim != 4 or corrected.shape[0] == 0 or corrected.shape[1] != 4
            or clean.shape != corrected.shape or sigma.shape != corrected.shape
            or not isinstance(border, int) or isinstance(border, bool) or border < 0
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
    energy = detail.square().sum(dim=(-2, -1))
    variance = (detail.square()*noise.square()).sum(dim=(-2, -1))
    numerator = ((corrected[crop]-target)*detail).sum(dim=(-2, -1)).square()
    supported = (energy > 0) & (variance > 0)
    denominator = torch.where(supported, variance, torch.ones_like(variance))
    return torch.where(supported, numerator/denominator, torch.zeros_like(numerator)).mean()
