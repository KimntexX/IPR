"""Observed local CFA residual in estimated noise units, never a clean input."""
import torch
from torch.nn import functional as F


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
    h, w = packed.shape[-2:]
    value = F.pad(packed, (1, 1, 1, 1), mode='reflect')
    variance = F.pad(sigma.square(), (1, 1, 1, 1), mode='reflect')
    pairs = (((0, -1), (0, 1)), ((-1, 0), (1, 0)),
        ((-1, -1), (1, 1)), ((-1, 1), (1, -1)))
    differences, noise = [], []
    for pair in pairs:
        a, b = [value[:, :, 1+y:1+y+h, 1+x:1+x+w] for y, x in pair]
        differences.append((packed-a)+(packed-b))
        a, b = [variance[:, :, 1+y:1+y+h, 1+x:1+x+w] for y, x in pair]
        noise.append(a+b)
    residual = ((differences[0]+differences[1])+(differences[2]+differences[3]))/8
    total_variance = sigma.square()+((noise[0]+noise[1])+(noise[2]+noise[3]))/64
    if not torch.isfinite(residual).all() or not torch.isfinite(total_variance).all():
        raise ValueError('Conditioning arithmetic overflow')
    normalized = residual/total_variance.sqrt().clamp_min(1e-12)
    return torch.where(sigma > 0, normalized.clamp(-8, 8), torch.zeros_like(normalized))
