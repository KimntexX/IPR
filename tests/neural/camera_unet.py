"""Bench-only small residual U-Net. No production hook or pretrained weights.

Contract: NCHW float32 balanced camera RGB and per-channel sigma, both finite.
Negative/above-white camera values are deliberately allowed, not clamped.
Architecture is a conventional two-level encoder/decoder written for this bench;
it is not a port of an author's repository or a qualified Canon denoiser.
"""
import math

import torch
from torch import nn
from torch.nn import functional as F


class Cancelled(RuntimeError):
    """No result is published when cancellation is observed."""


def validate(signal, sigma):
    if not isinstance(signal, torch.Tensor) or not isinstance(sigma, torch.Tensor):
        raise ValueError('signal and sigma must be tensors')
    if signal.dtype != torch.float32 or sigma.dtype != torch.float32:
        raise ValueError('float32 tensors required')
    if signal.ndim != 4 or signal.shape[1] != 3 or signal.shape[0] < 1:
        raise ValueError('nonempty NCHW RGB required')
    if min(signal.shape[2:]) < 8 or sigma.shape != signal.shape:
        raise ValueError('minimum extent 8; sigma must match all signal axes')
    if signal.device != sigma.device:
        raise ValueError('signal/sigma devices differ')
    if not torch.isfinite(signal).all() or not torch.isfinite(sigma).all():
        raise ValueError('nonfinite input')
    if (sigma < 0).any():
        raise ValueError('sigma cannot be negative')


def block(inputs, outputs):
    return nn.Sequential(nn.Conv2d(inputs, outputs, 3, padding=1), nn.SiLU(),
                         nn.Conv2d(outputs, outputs, 3, padding=1), nn.SiLU())


class CameraUNet(nn.Module):
    """Own random weights, six input channels, unconstrained residual RGB output."""

    def __init__(self, width=8):
        super().__init__()
        if not isinstance(width, int) or isinstance(width, bool) or not 4 <= width <= 64:
            raise ValueError('width must be an integer in [4,64]')
        self.width = width
        self.enc1 = block(6, width)
        self.enc2 = block(width, 2 * width)
        self.middle = block(2 * width, 4 * width)
        self.dec2 = block(6 * width, 2 * width)
        self.dec1 = block(3 * width, width)
        self.head = nn.Conv2d(width, 3, 1)
        # Exact initial identity is useful for contract checks, not learned quality.
        nn.init.zeros_(self.head.weight)
        nn.init.zeros_(self.head.bias)

    def forward(self, signal, sigma):
        validate(signal, sigma)
        e1 = self.enc1(torch.cat((signal, sigma), dim=1))
        e2 = self.enc2(F.avg_pool2d(e1, 2))
        middle = self.middle(F.avg_pool2d(e2, 2))
        d2 = self.dec2(torch.cat((F.interpolate(middle, size=e2.shape[2:],
                                               mode='nearest'), e2), dim=1))
        d1 = self.dec1(torch.cat((F.interpolate(d2, size=e1.shape[2:],
                                               mode='nearest'), e1), dim=1))
        return signal - self.head(d1)


def apply(model, signal, sigma, amount, cancelled=lambda: False):
    """Atomic bench call; amount [0,100], zero bypasses model exactly.

    Validation still runs at zero. GPU cancellation is not instant preemption.
    No in-place operation on the source or global runtime configuration.
    """
    validate(signal, sigma)
    if isinstance(amount, bool) or not isinstance(amount, (int, float)):
        raise ValueError('numeric amount required')
    if not math.isfinite(amount) or not 0 <= amount <= 100:
        raise ValueError('amount outside [0,100]')
    if cancelled():
        raise Cancelled('cancelled before inference')
    if amount == 0:
        return signal
    if not isinstance(model, nn.Module) or model.training:
        raise ValueError('evaluation model required')
    with torch.no_grad():
        predicted = model(signal, sigma)
        if not isinstance(predicted, torch.Tensor):
            raise ValueError('model must return a tensor')
        if predicted.shape != signal.shape or predicted.dtype != signal.dtype:
            raise ValueError('invalid model output contract')
        if predicted.device != signal.device or not torch.isfinite(predicted).all():
            raise ValueError('invalid model output values/device')
        result = signal + (amount / 100.0) * (predicted - signal)
    if cancelled():
        raise Cancelled('cancelled after inference; output discarded')
    if not torch.isfinite(result).all():
        raise ValueError('nonfinite blended output')
    return result
