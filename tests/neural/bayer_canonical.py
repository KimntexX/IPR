"""Exact spatial reflection to red-top-left/blue-bottom-right Bayer convention.

No interpolation, RGB-channel permutation, green relabelling or learned choice.
The same operation is its inverse. Physical sigma samples follow sensor samples.
"""
import torch
from torch.nn import functional as F

from joint_bayer_unet import validate


def spatial_reflect(image,flags):
    if (image.ndim!=4 or image.shape[0]<1 or flags.shape!=(image.shape[0],2) or flags.dtype!=torch.bool
            or image.device!=flags.device or not torch.isfinite(image).all()):
        raise ValueError('Finite NCHW image and matching N2 Boolean reflection flags required')
    rows=[]
    for i in range(len(image)):
        dimensions=[2+j for j in range(2) if bool(flags[i,j])]
        rows.append(image[i:i+1].flip(dimensions) if dimensions else image[i:i+1])
    return torch.cat(rows,dim=0)


def canonicalize(packed,sigma,pattern):
    validate(packed,sigma,pattern)
    red=(pattern==0).long().argmax(dim=1)
    blue=(pattern==2).long().argmax(dim=1)
    if (blue!=3-red).any():raise ValueError('Red and blue must occupy opposite Bayer corners')
    flags=torch.stack((red//2==1,red%2==1),dim=1)
    values=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(packed,2),flags),2)
    deviations=F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(sigma,2),flags),2)
    labels=spatial_reflect(pattern.reshape(-1,1,2,2),flags).reshape(-1,4)
    if (labels[:,0]!=0).any() or (labels[:,3]!=2).any():raise ValueError('Canonical phase mapping failed')
    return values,deviations,labels,flags


def predict(model,packed,sigma,pattern,exact_observations=False,canonical_input=False):
    if not isinstance(canonical_input,bool):raise ValueError('Explicit Boolean representation policy required')
    if not canonical_input:return model(packed,sigma,pattern,exact_observations=exact_observations)
    values,deviations,labels,flags=canonicalize(packed,sigma,pattern)
    output=model(values,deviations,labels,exact_observations=exact_observations)
    return spatial_reflect(output,flags)
