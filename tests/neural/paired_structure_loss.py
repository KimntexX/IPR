"""Bench-only cross-realization coherent bias penalty, not an inference filter."""
import torch


def coherent_projection(corrected, clean, reference_sigma, border):
    """Reference variance must be fixed given the clean training target."""
    if (corrected.ndim != 4 or corrected.shape[0] == 0 or corrected.shape[1] != 4
            or clean.shape != corrected.shape or reference_sigma.shape != corrected.shape
            or isinstance(border,bool) or not isinstance(border,int) or border < 0
            or min(corrected.shape[-2:]) <= 2*border
            or any(x.dtype != torch.float32 or x.device != corrected.device or not torch.isfinite(x).all()
                for x in (corrected,clean,reference_sigma)) or (reference_sigma < 0).any()):
        raise ValueError('Matching finite float32 CFA, fixed nonnegative sigma and valid core required')
    crop = (slice(None),slice(None),slice(border,-border or None),slice(border,-border or None))
    truth = clean[crop].detach()
    detail = truth-truth[:,:,:1,:1]
    detail = detail-detail.mean(dim=(-2,-1),keepdim=True)
    variance = (detail.square()*reference_sigma[crop].detach().square()).sum(dim=(-2,-1))
    numerator = ((corrected[crop]-truth)*detail).sum(dim=(-2,-1))
    return torch.where(variance > 0,numerator/torch.where(variance > 0,variance,torch.ones_like(variance)).sqrt(),torch.zeros_like(numerator))


def paired_structure_loss(first,second,clean,reference_sigma,border,mode):
    """Compare equal-cost paired squares with independent-draw cross products.

    E[q1*q2|clean] = E[q|clean]^2 only for conditionally independent draws,
    identical deterministic network and fixed reference normalization. The cross
    estimate can be negative: never clip it or interpret a single draw as bias.
    """
    if mode not in ('paired_square','paired_cross'):
        raise ValueError('Explicit paired structure mode required')
    a = coherent_projection(first,clean,reference_sigma,border)
    b = coherent_projection(second,clean,reference_sigma,border)
    return (.5*(a.square()+b.square()) if mode == 'paired_square' else a*b).mean()
