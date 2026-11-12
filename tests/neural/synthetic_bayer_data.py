"""Known procedural camera-RGB signals and explicitly uncalibrated CFA noise."""
import math

import torch

from joint_bayer_unet import sample_rgb


PATTERNS=((0,1,3,2),(1,0,2,3),(3,2,0,1),(2,3,1,0))
FAMILIES=('constant','ramp','edge','weak_achromatic_wave','chromatic_wave')


def signal(family,seed,side=128):
    if family not in FAMILIES or side<32 or side%2:raise ValueError('Known family and even extent>=32 required')
    generator=torch.Generator().manual_seed(seed)
    params=torch.rand(8,generator=generator)
    y,x=torch.meshgrid(torch.linspace(-1,1,side),torch.linspace(-1,1,side),indexing='ij')
    direction=x*torch.cos(params[0]*math.pi)+y*torch.sin(params[0]*math.pi)
    base=.05+.65*params[1:4,None,None]
    if family=='constant':image=base.expand(3,side,side).clone()-.08
    elif family=='ramp':image=base+direction[None]*(.1+.35*params[4:7,None,None])
    elif family=='edge':
        weight=(direction>.4*(params[7]-.5)).float()[None]
        other=.08+.8*params[4:7,None,None]
        image=base*(1-weight)+other*weight
    else:
        phase=2*math.pi*(2.1+.9*params[4])*direction+params[5]*2*math.pi
        if family=='weak_achromatic_wave':image=base+.015*torch.sin(phase)[None]
        else:image=base+.12*torch.stack([torch.sin(phase+i*2*math.pi/3) for i in range(3)])
    return image.float()


def noisy_observation(rgb,pattern,a,b,generator):
    if a<0 or b<0:raise ValueError('Nonnegative simulated variance coefficients required')
    clean=sample_rgb(rgb,pattern)
    variance=a*clean.clamp_min(0)+b
    noise=torch.randn(clean.shape,generator=generator,dtype=clean.dtype,device=clean.device)*variance.sqrt()
    noisy=clean+noise
    # Conditioning uses observation + known simulation coefficients, not clean sigma.
    sigma=(a*noisy.clamp_min(0)+b).sqrt()
    return noisy,sigma
