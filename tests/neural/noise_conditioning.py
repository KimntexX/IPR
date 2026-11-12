"""Observed quadratic-fit CFA residual in estimated noise units, no clean input."""
import torch

from polynomial_residual import residual_and_variance


def standardized_residual(packed, sigma):
    if (not isinstance(packed, torch.Tensor) or not isinstance(sigma, torch.Tensor)
            or packed.ndim != 4 or packed.shape[0] < 1 or packed.shape[1] != 4
            or sigma.shape != packed.shape or packed.dtype != torch.float32
            or sigma.dtype != packed.dtype or sigma.device != packed.device
            or min(packed.shape[-2:]) < 8 or not torch.isfinite(packed).all()
            or not torch.isfinite(sigma).all() or (sigma < 0).any()):
        raise ValueError('Finite matching N4HW CFA observation and nonnegative sigma required')
    if (sigma == 0).all():
        return torch.zeros_like(packed)
    residual, total_variance = residual_and_variance(packed, sigma)
    if not torch.isfinite(residual).all() or not torch.isfinite(total_variance).all():
        raise ValueError('Conditioning arithmetic overflow')
    normalized = residual/total_variance.sqrt().clamp_min(1e-12)
    return torch.where(sigma > 0, normalized.clamp(-8, 8), torch.zeros_like(normalized))
