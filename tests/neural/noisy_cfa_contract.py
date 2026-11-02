"""Synthetic noisy-CFA contract; no network or claimed Canon noise calibration."""
from dataclasses import dataclass
import math

import torch

from camera_data import checked_scale, masked_mse
from joint_bayer_unet import sample_rgb, validate
from synthetic_bayer_data import noisy_observation


@dataclass(frozen=True)
class NoisyCfaExample:
    observation: torch.Tensor
    target_rgb: torch.Tensor
    sigma: torch.Tensor
    pattern: torch.Tensor
    scale: float
    variance_a: float
    variance_b: float
    seed: int


def prepare_synthetic(target_rgb, pattern, a, b, seed, scale=2.):
    if (not isinstance(target_rgb, torch.Tensor) or target_rgb.dtype != torch.float32 or target_rgb.ndim != 4 or target_rgb.shape[1] != 3
            or not torch.isfinite(target_rgb).all() or target_rgb.device.type != 'cpu'):
        raise ValueError('Finite CPU float32 full-resolution RGB synthetic truth required')
    if not isinstance(pattern, torch.Tensor):
        raise ValueError('Explicit Bayer pattern tensor required')
    if (any(isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in (a, b))
            or isinstance(seed, bool) or not isinstance(seed, int) or seed < 0):
        raise ValueError('Known finite nonnegative synthetic coefficients and integer seed required')
    scale = checked_scale(scale)
    # sample_rgb validates geometry and physical Bayer labels before noise synthesis.
    clean = sample_rgb(target_rgb, pattern)
    noisy, sigma = noisy_observation(target_rgb, pattern, a, b, torch.Generator().manual_seed(seed))
    validate(noisy, sigma, pattern)
    normalized = (noisy/scale, target_rgb/scale, sigma/scale)
    for original, value in zip((noisy, target_rgb, sigma), normalized):
        if not torch.isfinite(value).all() or ((original != 0) & (value == 0)).any():
            raise ValueError('Normalization destroys finite signal or sigma')
    if a == b == 0 and not torch.equal(noisy, clean):
        raise ValueError('Zero-noise sensor observation changed')
    return NoisyCfaExample(*normalized, pattern.clone(), scale, float(a), float(b), seed)


def joint_loss(predicted_rgb, example, valid):
    """All RGB components are supervised, including values at observed sites."""
    if not isinstance(example, NoisyCfaExample):
        raise ValueError('Prepared noisy-CFA example required')
    return masked_mse(predicted_rgb, example.target_rgb, valid)
