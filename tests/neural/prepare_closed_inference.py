"""Freeze a copy's projected kernels once; no source mutation or production hook."""
from copy import deepcopy

import torch
from torch import nn

from closed_moment_unet import ClosedMomentUNet, TransposeConv


def prepare_inference(model):
    if not isinstance(model, ClosedMomentUNet) or not model.equivariant or model.training:
        raise ValueError('Evaluated equivariant model required')
    if not all(torch.isfinite(p).all() for p in model.parameters()):
        raise ValueError('Finite trained parameters required')
    prepared = deepcopy(model)

    def replace(parent):
        for name, child in tuple(parent.named_children()):
            if isinstance(child, TransposeConv):
                devices = [child.weight.device.index] if child.weight.device.type == 'cuda' else []
                with torch.random.fork_rng(devices=devices):
                    convolution = nn.Conv2d(child.in_channels, child.out_channels, child.kernel_size,
                        padding=child.padding, device=child.weight.device, dtype=child.weight.dtype)
                with torch.no_grad():
                    mapped = child.weight[child.output_permutation][:, child.input_permutation].transpose(-2, -1)
                    convolution.weight.copy_(child.weight*.5+mapped*.5)
                    convolution.bias.copy_(child.bias*.5+child.bias[child.output_permutation]*.5)
                setattr(parent, name, convolution)
            else:
                replace(child)

    replace(prepared)
    prepared.requires_grad_(False)
    return prepared.eval()
