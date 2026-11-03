"""One fixed observation-only protection probe, not a calibrated edge detector."""
import math
import torch
from torch.nn import functional as F

from joint_bayer_unet import validate
from noisy_moment_unet import NoisyMomentUNet

HALO_RADIUS = 3
COMPARISON_COUNT = 4*3*(2*HALO_RADIUS+1)**2
NOMINAL_FAMILY_ALPHA = .01
SCORE_ONSET = math.sqrt(2*math.log(2*COMPARISON_COUNT/NOMINAL_FAMILY_ALPHA))


def edge_confidence(packed, sigma):
    """Attenuate correction near non-affine contrast above estimated noise.

    Second-order row/column/mixed contrasts exclude the central sample and vanish on
    affine interiors. Four planes share confidence. A nominal Gaussian union
    bound accounts for comparisons; estimated sigma prevents a probability claim.
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
    horizontal += 9*(variance[:, :, :-2, 1:-1]+variance[:, :, 2:, 1:-1])
    vertical += 9*(variance[:, :, 1:-1, :-2]+variance[:, :, 1:-1, 2:])
    horizontal_curvature = left+right-3*(values[:, :, :-2, 1:-1]+values[:, :, 2:, 1:-1])
    vertical_curvature = up+down-3*(values[:, :, 1:-1, :-2]+values[:, :, 1:-1, 2:])
    mixed_curvature = (values[:, :, :-2, :-2]+values[:, :, 2:, 2:])-(values[:, :, :-2, 2:]+values[:, :, 2:, :-2])
    mixed_variance = (variance[:, :, :-2, :-2]+variance[:, :, 2:, 2:])+(variance[:, :, :-2, 2:]+variance[:, :, 2:, :-2])
    zh = horizontal_curvature.abs()/horizontal.clamp_min(1e-24).sqrt()
    zv = vertical_curvature.abs()/vertical.clamp_min(1e-24).sqrt()
    zm = mixed_curvature.abs()/mixed_variance.clamp_min(1e-24).sqrt()
    score = torch.maximum(torch.maximum(zh, zv), zm).amax(dim=1, keepdim=True)
    if not torch.isfinite(score).all():
        raise ValueError('Nonfinite contrast/noise score')
    score = F.max_pool2d(score, 2*HALO_RADIUS+1, stride=1, padding=HALO_RADIUS)
    return (-.5*(score-SCORE_ONSET).clamp(0, 20).square()).exp()


class EdgeProtectedNoisyUNet(NoisyMomentUNet):
    """Same learned parameters; only CFA correction is multiplied by confidence."""
    def correct_observations(self, packed, sigma, pattern):
        validate(packed, sigma, pattern)
        if (sigma == 0).all():
            return packed
        corrected = super().correct_observations(packed, sigma, pattern)
        return packed+edge_confidence(packed, sigma)*(corrected-packed)
