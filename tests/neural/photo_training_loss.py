"""Bench objectives: masked pixel fidelity and optional per-patch/channel mean error."""
import math

import torch

from camera_data import masked_mse


def channel_bias_loss(prediction,target,valid,border=24):
    if prediction.shape!=target.shape or prediction.ndim!=4 or prediction.shape[1]!=3:
        raise ValueError('Matching NCHW RGB required')
    if valid.shape!=(prediction.shape[0],1,*prediction.shape[2:]) or valid.dtype!=torch.bool:
        raise ValueError('Matching Boolean N1HW mask required')
    if border<0 or min(prediction.shape[2:])<=2*border:raise ValueError('Insufficient interior support')
    if not torch.isfinite(prediction).all() or not torch.isfinite(target).all():raise ValueError('Nonfinite loss input')
    region=(slice(None),slice(None),slice(border,-border or None),slice(border,-border or None))
    mask=valid[region]
    counts=mask.sum(dim=(2,3))
    if (counts==0).any():raise ValueError('Each patch needs nonempty interior support')
    errors=(prediction-target)[region]
    means=(errors*mask).sum(dim=(2,3))/counts
    return means.square().mean()


def objective(prediction,target,valid,weight=0.,border=24):
    if isinstance(weight,bool) or not isinstance(weight,(float,int)) or not math.isfinite(weight) or weight<0:
        raise ValueError('Finite nonnegative bias weight required')
    pixel=masked_mse(prediction,target,valid)
    penalty=channel_bias_loss(prediction,target,valid,border) if weight else pixel.new_zeros(())
    total=pixel+weight*penalty
    if not torch.isfinite(total):raise ValueError('Nonfinite objective')
    return total,pixel,penalty
