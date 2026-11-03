"""One fixed observation-only protection probe, not a calibrated edge detector."""
import torch
from torch.nn import functional as F

from joint_bayer_unet import validate
from noisy_moment_unet import NoisyMomentUNet


def edge_confidence(packed, sigma):
    """Attenuate correction near coherent contrast above an estimated noise scale.

    Row/column differences exclude the central sample, reducing sensitivity to
    isolated noise there. Four physical planes share a conservative confidence.
    Three-sigma onset is a fixed heuristic, not a calibrated false-positive rate.
    The three-cell halo protects interpolation support and local features.
    """
    if (packed.ndim != 4 or packed.shape[1] != 4 or sigma.shape != packed.shape
            or packed.dtype != torch.float32 or sigma.dtype != packed.dtype or sigma.device != packed.device
            or min(packed.shape[-2:]) < 8 or not torch.isfinite(packed).all()
            or not torch.isfinite(sigma).all() or (sigma < 0).any()):
        raise ValueError('Finite matching CFA/sigma fields required')
    if (sigma == 0).all():
        return packed.new_zeros(len(packed), 1, *packed.shape[-2:])
    values = F.pad(packed, (1, 1, 1, 1), mode='reflect')
    variance = F.pad(sigma.square(), (1, 1, 1, 1), mode='reflect')
    left = values[:, :, :-2, :-2]+values[:, :, 1:-1, :-2]+values[:, :, 2:, :-2]
    right = values[:, :, :-2, 2:]+values[:, :, 1:-1, 2:]+values[:, :, 2:, 2:]
    up = values[:, :, :-2, :-2]+values[:, :, :-2, 1:-1]+values[:, :, :-2, 2:]
    down = values[:, :, 2:, :-2]+values[:, :, 2:, 1:-1]+values[:, :, 2:, 2:]
    horizontal = sum(variance[:, :, y:y+packed.shape[-2], x:x+packed.shape[-1]]
        for y in (0, 1, 2) for x in (0, 2))
    vertical = sum(variance[:, :, y:y+packed.shape[-2], x:x+packed.shape[-1]]
        for y in (0, 2) for x in (0, 1, 2))
    zh = (right-left).abs()/horizontal.clamp_min(1e-24).sqrt()
    zv = (down-up).abs()/vertical.clamp_min(1e-24).sqrt()
    score = torch.maximum(zh, zv).amax(dim=1, keepdim=True)
    if not torch.isfinite(score).all():
        raise ValueError('Nonfinite contrast/noise score')
    score = F.max_pool2d(score, 7, stride=1, padding=3)
    return (-.5*(score-3).clamp(0, 20).square()).exp()


class EdgeProtectedNoisyUNet(NoisyMomentUNet):
    """Same learned parameters; only CFA correction is multiplied by confidence."""
    def correct_observations(self, packed, sigma, pattern):
        validate(packed, sigma, pattern)
        if (sigma == 0).all():
            return packed
        corrected = super().correct_observations(packed, sigma, pattern)
        return packed+edge_confidence(packed, sigma)*(corrected-packed)
