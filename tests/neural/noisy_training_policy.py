"""Explicit study masks: joint fitting versus CFA correction with frozen trunk."""
from noisy_moment_unet import NoisyMomentUNet


def configure_trainable(model, scope, statistics_trainable=True):
    if not isinstance(model, NoisyMomentUNet) or scope not in ('joint', 'cfa_front') or not isinstance(statistics_trainable, bool):
        raise ValueError('Declared noisy-model training scope required')
    trainable, frozen = 0, 0
    for name, parameter in model.named_parameters():
        enabled = scope == 'joint' or name.startswith(('noise_features.', 'noise_head.'))
        if name.startswith('noise_features.statistics.') and not statistics_trainable:
            enabled = False
        parameter.requires_grad_(enabled)
        if enabled:
            trainable += parameter.numel()
        else:
            frozen += parameter.numel()
    return dict(scope=scope, trainable_parameters=trainable, frozen_parameters=frozen,
        statistics_trainable=statistics_trainable)
