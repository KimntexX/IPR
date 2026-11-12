"""Bench ablation: short local-difference route to the36-coefficient head."""
import torch
from torch import nn
from torch.nn import functional as F

from closed_moment_unet import ClosedMomentUNet, TransposeConv, equivariant_block, paired_permutation
from closed_moment_operator import TRANSPOSE_PERMUTATION, interpolate_closed
from joint_bayer_unet import validate, colour_tags


# Four features per physical plane: |central H/V slope|, |H/V curvature|.
# Transpose swaps spatial planes TR/BL and H/V features, without sign changes.
LOCAL_PERMUTATION = tuple(4*p+d for p in (0, 2, 1, 3) for d in (1, 0, 3, 2))


def local_descriptors(packed):
    if (not isinstance(packed, torch.Tensor) or packed.ndim != 4 or packed.shape[1] != 4
            or packed.dtype != torch.float32 or min(packed.shape[-2:]) < 2 or not torch.isfinite(packed).all()):
        raise ValueError('Finite float32 N4HW spatial CFA planes required')
    padded = F.pad(packed, (1, 1, 1, 1), mode='reflect')
    left, right = padded[:, :, 1:-1, :-2], padded[:, :, 1:-1, 2:]
    up, down = padded[:, :, :-2, 1:-1], padded[:, :, 2:, 1:-1]
    features = torch.stack(((right-left).abs()*.5, (down-up).abs()*.5,
        ((right-packed)+(left-packed)).abs(), ((down-packed)+(up-packed)).abs()), dim=2)
    return features.reshape(len(packed), 16, *packed.shape[-2:])


class LocalMomentUNet(ClosedMomentUNet):
    def __init__(self, width=8):
        super().__init__(width, equivariant=True)
        self.local_features = equivariant_block(LOCAL_PERMUTATION, paired_permutation(width))
        self.local_head = TransposeConv(paired_permutation(width), TRANSPOSE_PERMUTATION, 1)
        nn.init.zeros_(self.local_head.weight)
        nn.init.zeros_(self.local_head.bias)

    def forward(self, packed, sigma, pattern, exact_observations=False):
        validate(packed, sigma, pattern)
        if exact_observations is not True or (sigma != 0).any():
            raise ValueError('Declared noise-free observations required for this reconstruction ablation')
        tags = colour_tags(pattern).reshape(len(packed), 12, 1, 1).expand(-1, -1, *packed.shape[2:])
        a = self.enc1(torch.cat((packed, sigma, tags), dim=1))
        b = self.enc2(F.avg_pool2d(a, 2))
        c = self.middle(F.avg_pool2d(b, 2))
        d = self.dec2(torch.cat((F.interpolate(c, size=b.shape[2:], mode='nearest'), b), dim=1))
        e = self.dec1(torch.cat((F.interpolate(d, size=a.shape[2:], mode='nearest'), a), dim=1))
        logits = self.head(e)+self.local_head(self.local_features(local_descriptors(packed)))
        return interpolate_closed(packed, pattern, logits.tanh())
