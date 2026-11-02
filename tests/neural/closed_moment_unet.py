"""Bench-only36-coefficient models, optionally transpose-equivariant by construction."""
import torch
from torch import nn
from torch.nn import functional as F

from camera_unet import block
from closed_moment_operator import CHANNEL_COUNT, TRANSPOSE_PERMUTATION, interpolate_closed
from joint_bayer_unet import validate, colour_tags


class TransposeConv(nn.Conv2d):
    """Project W onto W = Pout * spatial_transpose(W) * Pin; biases follow Pout."""
    def __init__(self, inputs, outputs, kernel=3):
        super().__init__(len(inputs), len(outputs), kernel, padding=kernel//2)
        for permutation in (inputs, outputs):
            if sorted(permutation) != list(range(len(permutation))) or any(permutation[permutation[i]] != i for i in range(len(permutation))):
                raise ValueError('Involutive input/output channel permutations required')
        self.register_buffer('input_permutation', torch.tensor(inputs))
        self.register_buffer('output_permutation', torch.tensor(outputs))

    def forward(self, image):
        transformed = self.weight[self.output_permutation][:, self.input_permutation].transpose(-2, -1)
        weight = self.weight*.5+transformed*.5
        bias = self.bias*.5+self.bias[self.output_permutation]*.5
        return F.conv2d(image, weight, bias, padding=self.padding)


def paired_permutation(channels):
    return tuple(i ^ 1 for i in range(channels))


def equivariant_block(inputs, outputs):
    return nn.Sequential(TransposeConv(inputs, outputs), nn.SiLU(),
        TransposeConv(outputs, outputs), nn.SiLU())


class ClosedMomentUNet(nn.Module):
    def __init__(self, width=8, equivariant=False):
        super().__init__()
        if isinstance(width, bool) or not isinstance(width, int) or not 4 <= width <= 64 or width % 2 or not isinstance(equivariant, bool):
            raise ValueError('Even integer width4..64 and explicit equivariance policy required')
        self.width = width
        self.equivariant = equivariant
        if equivariant:
            inputs = tuple(4*j+i for j in range(5) for i in (0, 2, 1, 3))
            p1, p2, p4 = (paired_permutation(k*width) for k in (1, 2, 4))
            concatenate = lambda first, second: first+tuple(len(first)+i for i in second)
            self.enc1 = equivariant_block(inputs, p1)
            self.enc2 = equivariant_block(p1, p2)
            self.middle = equivariant_block(p2, p4)
            self.dec2 = equivariant_block(concatenate(p4, p2), p2)
            self.dec1 = equivariant_block(concatenate(p2, p1), p1)
            self.head = TransposeConv(p1, TRANSPOSE_PERMUTATION, 1)
        else:
            self.enc1 = block(20, width)
            self.enc2 = block(width, 2*width)
            self.middle = block(2*width, 4*width)
            self.dec2 = block(6*width, 2*width)
            self.dec1 = block(3*width, width)
            self.head = nn.Conv2d(width, CHANNEL_COUNT, 1)
        nn.init.zeros_(self.head.weight)
        nn.init.zeros_(self.head.bias)

    def forward(self, packed, sigma, pattern, exact_observations=False):
        validate(packed, sigma, pattern)
        if exact_observations is not True or (sigma != 0).any():
            raise ValueError('Declared noise-free observations required in this stage')
        tags = colour_tags(pattern).reshape(len(packed), 12, 1, 1).expand(-1, -1, *packed.shape[2:])
        a = self.enc1(torch.cat((packed, sigma, tags), dim=1))
        b = self.enc2(F.avg_pool2d(a, 2))
        c = self.middle(F.avg_pool2d(b, 2))
        d = self.dec2(torch.cat((F.interpolate(c, size=b.shape[2:], mode='nearest'), b), dim=1))
        e = self.dec1(torch.cat((F.interpolate(d, size=a.shape[2:], mode='nearest'), a), dim=1))
        return interpolate_closed(packed, pattern, torch.tanh(self.head(e)))


def build_model(kind, width):
    if kind not in ('closed', 'equivariant'):
        raise ValueError('Declared closed-basis experiment kind required')
    return ClosedMomentUNet(width, equivariant=kind == 'equivariant')
