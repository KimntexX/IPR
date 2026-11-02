"""Training-only energy balancing; no change to reconstruction qualification gates."""
import math

import torch


def energy_balanced_loss(image, target, reference, scale, border=48, objective='relative'):
    """RGB MSE normalized by reference energy, in physical signal units.

    All tensors enter in network units (physical RGB / scale). Only the
    prediction has a gradient. The denominator is per example, never per
    channel/pixel; weak colours and edges keep their RGB geometry.
    Tempered normalization uses sqrt(1e-6 * max(reference MSE, 1e-6)),
    a fixed halfway energy exponent; relative uses max(reference MSE,1e-6).
    """
    if (objective not in ('relative', 'tempered') or not math.isfinite(scale) or scale <= 0 or not isinstance(border, int)
            or border < 0 or image.ndim != 4 or image.shape[1] != 3
            or image.shape != target.shape or image.shape != reference.shape
            or min(image.shape[-2:]) <= 2*border or image.shape[0] == 0):
        raise ValueError('Matching nonempty RGB batches, valid crop and positive scale required')
    crop = (slice(None), slice(None), slice(border, -border or None), slice(border, -border or None))
    prediction, truth, base = image[crop], target[crop].detach(), reference[crop].detach()
    if not all(torch.isfinite(t).all() for t in (prediction, truth, base)):
        raise ValueError('Finite reconstruction tensors required')
    error = (prediction-truth).square().mean(dim=(1, 2, 3))
    denominator = (base-truth).square().mean(dim=(1, 2, 3)).clamp_min(1e-6/scale**2)
    if objective == 'tempered':
        denominator = (denominator*(1e-6/scale**2)).sqrt()
    return (error/denominator).mean()
