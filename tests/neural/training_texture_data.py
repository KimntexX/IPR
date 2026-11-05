"""Reconstruct procedural training textures with declared per-channel signs."""
import math

import torch

from context_data import extend_texture
from fine_texture_training import texture
from texture_sampling import texture_identity


def apply_colour_flips(image,metadata,flips):
    if (not isinstance(flips,(list,tuple)) or len(flips)!=3
            or any(isinstance(x,bool) or not isinstance(x,int) or x not in (-1,1) for x in flips)
            or image.ndim!=4 or image.shape[0:2]!=(1,3) or image.dtype!=torch.float32
            or not torch.isfinite(image).all() or not math.isfinite(metadata['dc'])
            or len(metadata['colours'])!=3 or any(not math.isfinite(x) for x in metadata['colours'])):
        raise ValueError('Finite float32 RGB training texture and three explicit integer signs required')
    adjusted=dict(metadata,colours=[float(c*s) for c,s in zip(metadata['colours'],flips)])
    if all(x==1 for x in flips):
        return image,adjusted
    mask=torch.tensor(flips,device=image.device).reshape(1,3,1,1)<0
    return torch.where(mask,2*metadata['dc']-image,image),adjusted


def training_texture(event):
    image,metadata=texture(event['family'],texture_identity(event),128)
    return apply_colour_flips(image,metadata,event.get('colour_flips',(1,1,1)))


def training_context(event,side):
    identity=texture_identity(event)
    _,metadata=texture(event['family'],identity,128)
    image=extend_texture(event['family'],identity,side)
    return apply_colour_flips(image,metadata,event.get('colour_flips',(1,1,1)))[0]
