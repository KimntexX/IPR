"""Explicit study masks: joint fitting versus CFA correction with frozen trunk."""
from noisy_moment_unet import NoisyMomentUNet


def configure_trainable(model, scope):
    if not isinstance(model, NoisyMomentUNet) or scope not in ('joint', 'cfa_front'):
        raise ValueError('Declared noisy-model training scope required')
    trainable, frozen = 0, 0
    for name, parameter in model.named_parameters():
        enabled = scope == 'joint' or name.startswith(('noise_features.', 'noise_head.', 'noise_residual.'))
        parameter.requires_grad_(enabled)
        if enabled:
            trainable += parameter.numel()
        else:
            frozen += parameter.numel()
    return dict(scope=scope, trainable_parameters=trainable, frozen_parameters=frozen)
