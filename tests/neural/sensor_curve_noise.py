"""Measured-law Gaussian surrogate in balanced camera units; not a sensor emulator."""
import torch
import math
from joint_bayer_unet import sample_rgb


def observation(truth,tags,scenario,seed):
    if truth.ndim!=4 or truth.shape[0]!=1 or tags.shape!=(1,4) or truth.device.type!='cpu' or tags.device.type!='cpu':
        raise ValueError('One CPU truth and one physical Bayer phase per microbatch required')
    clean=sample_rgb(truth,tags)
    if scenario is None:return clean,torch.zeros_like(clean)
    by_colour={p['colour']:p for p in scenario['parameters']}
    if len(scenario['parameters'])!=4 or set(by_colour)!={0,1,2,3}:raise ValueError('Four distinct Bayer site coefficients required')
    a=[];b=[]
    for colour in tags[0].tolist():
        p=by_colour[colour]
        if not all(math.isfinite(p[k]) for k in ('a','b','gain','divisor')) or min(p['a'],p['b'])<0 or min(p['gain'],p['divisor'])<=0:
            raise ValueError('Valid physical variance and normalization required')
        ratio=p['gain']/p['divisor']
        a.append(p['a']*ratio);b.append(p['b']*ratio*ratio)
    aa=torch.tensor(a,dtype=clean.dtype).reshape(1,4,1,1)
    bb=torch.tensor(b,dtype=clean.dtype).reshape(1,4,1,1)
    if not torch.isfinite(aa).all() or not torch.isfinite(bb).all() or (aa<0).any() or (bb<0).any():
        raise ValueError('Finite nonnegative balanced-camera variance law required')
    generator=torch.Generator().manual_seed(seed)
    noisy=clean+torch.randn(clean.shape,generator=generator)*torch.sqrt(aa*clean.clamp_min(0)+bb)
    sigma=torch.sqrt(aa*noisy.clamp_min(0)+bb)
    return noisy,sigma
