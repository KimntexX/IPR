"""Training-only clean-signal fixed-point anchor in physical CFA coordinates."""
import math

import torch


def clean_cfa_loss(corrected, clean, reference_rgb, target_rgb, scale, border=48):
    if (not isinstance(scale, (int, float)) or isinstance(scale, bool)
            or not math.isfinite(scale) or scale <= 0 or isinstance(border, bool)
            or not isinstance(border, int) or border < 0 or border % 2
            or corrected.ndim != 4 or corrected.shape[1] != 4 or corrected.shape != clean.shape
            or reference_rgb.ndim != 4 or reference_rgb.shape[1] != 3 or reference_rgb.shape != target_rgb.shape
            or reference_rgb.shape[0] != corrected.shape[0] or corrected.shape[0] == 0
            or reference_rgb.shape[-2:] != (2*corrected.shape[-2], 2*corrected.shape[-1])
            or min(reference_rgb.shape[-2:]) <= 2*border):
        raise ValueError('Matching CFA/RGB extents, positive scale and even core border required')
    arrays = (corrected, clean, reference_rgb, target_rgb)
    if any(a.dtype != torch.float32 or a.device != corrected.device or not torch.isfinite(a).all() for a in arrays):
        raise ValueError('Finite matching float32 training arrays required')
    sensor_border = border//2
    crop = (slice(None), slice(None), slice(sensor_border, -sensor_border or None),
        slice(sensor_border, -sensor_border or None))
    rgb_crop = (slice(None), slice(None), slice(border, -border or None), slice(border, -border or None))
    error = (corrected[crop]-clean.detach()[crop]).square().mean(dim=(1, 2, 3))
    energy = (reference_rgb.detach()[rgb_crop]-target_rgb.detach()[rgb_crop]).square().mean(dim=(1, 2, 3))
    denominator = (energy.clamp_min(1e-6/scale**2)*(1e-6/scale**2)).sqrt()
    return (error/denominator).mean()
