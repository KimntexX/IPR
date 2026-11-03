"""Bench-only joint learning: sigma-bounded CFA correction then local reconstruction."""
import torch
from torch import nn

from closed_moment_unet import TransposeConv, paired_permutation
from joint_bayer_unet import validate, colour_tags
from local_moment_unet import LocalMomentUNet, LOCAL_PERMUTATION, local_descriptors
from noise_conditioning import standardized_residual
from multiscale_correction import CfaContextFeatures


FORWARD_TOOLS = ('noisy_moment_unet.py', 'noise_conditioning.py', 'polynomial_residual.py',
    'local_moment_unet.py', 'closed_moment_unet.py', 'closed_moment_operator.py',
    'moment_bayer_unet.py', 'joint_bayer_unet.py', 'camera_unet.py', 'bayer_canonical.py',
    'multiscale_correction.py')


class NoisyMomentUNet(LocalMomentUNet):
    """Own experimental weights; no clipping, profile or production integration.

    Canonical physical CFA planes remain distinct. A two-level equivariant
    context predicts a correction bounded by three supplied deviations.
    The study config explicitly freezes the trunk or permits joint fitting.
    Sigma is an input estimate, not a guarantee of physical sensor calibration.
    """
    def __init__(self, width=8):
        super().__init__(width)
        physical = (0, 2, 1, 3)
        inputs = tuple(4*j+i for j in range(5) for i in physical)
        inputs += tuple(20+i for i in LOCAL_PERMUTATION)
        inputs += tuple(36+i for i in physical)
        self.noise_features = CfaContextFeatures(inputs, width)
        self.noise_head = TransposeConv(paired_permutation(width), physical, 1)
        nn.init.zeros_(self.noise_head.weight)
        nn.init.zeros_(self.noise_head.bias)

    def correct_observations(self, packed, sigma, pattern):
        validate(packed, sigma, pattern)
        if (sigma == 0).all():
            return packed
        tags = colour_tags(pattern).reshape(len(packed), 12, 1, 1).expand(-1, -1, *packed.shape[2:])
        features = torch.cat((packed, sigma, tags, local_descriptors(packed), standardized_residual(packed, sigma)), dim=1)
        hidden = self.noise_features(features)
        correction = self.noise_head(hidden).tanh()*(3*sigma)
        corrected = packed+correction
        if not torch.isfinite(corrected).all():
            raise ValueError('Nonfinite corrected observation')
        return corrected

    def forward(self, packed, sigma, pattern, exact_observations=False):
        validate(packed, sigma, pattern)
        if not isinstance(exact_observations, bool):
            raise ValueError('Explicit Boolean observation policy required')
        if exact_observations and (sigma != 0).any():
            raise ValueError('No exact-observation projection for noisy input')
        corrected = self.correct_observations(packed, sigma, pattern)
        return super().forward(corrected, torch.zeros_like(sigma), pattern, exact_observations=True)


def load_reconstruction(model, state):
    """Initialize from a qualified reconstruction, rejecting other missing keys."""
    expected = {k for k in model.state_dict() if k.startswith(('noise_features.', 'noise_head.'))}
    supplied = set(state)
    if supplied != set(model.state_dict())-expected:
        raise ValueError('Exact reconstruction state required, no partial checkpoint loading')
    missing, unexpected = model.load_state_dict(state, strict=False)
    if set(missing) != expected or unexpected:
        raise ValueError('Unexpected checkpoint contract')
