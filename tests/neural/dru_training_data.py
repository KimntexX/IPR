"""Streaming procedural bootstrap; no photographs, reserved targets or cached contexts."""
import torch
from context_data import extend_signal,extend_texture
from joint_bayer_unet import sample_rgb
from sensor_curve_noise import observation
from bayer_canonical import canonicalize
from synthetic_bayer_data import PATTERNS


def example(serial,seed,scenarios,side=1024):
    if isinstance(serial,bool) or not isinstance(serial,int) or serial<0 or side<1024 or side%16:
        raise ValueError('Nonnegative serial and full qualified context required')
    identity=280000000+seed*100000+serial
    family=serial%8
    if family<2:
        truth=extend_texture(('weak_achromatic_wave','chromatic_wave')[family],identity,side)
    else:
        truth=extend_signal(('constant','ramp','edge','weak_achromatic_wave','chromatic_wave','edge')[family-2],identity,side)
        # Whole-context affine transform preserves the analytic signal, without clipping.
        draw=torch.rand(3,generator=torch.Generator().manual_seed(identity+50000000))
        truth=truth*(.2+1.6*float(draw[0]))+(-.06+.12*float(draw[1]))
    tags=torch.tensor([PATTERNS[(serial//8+serial)%4]],dtype=torch.int64)
    scenario=None if serial%127==0 else scenarios[(serial//32+serial//4)%len(scenarios)]
    values,sigma=observation(truth,tags,scenario,identity+90000000)
    clean=sample_rgb(truth,tags)
    packed,deviation,labels,_=canonicalize(values/2,sigma/2,tags)
    target,_,_,_=canonicalize(clean/2,torch.zeros_like(clean),tags)
    return packed,deviation,labels,target


def core_loss(prediction,target,sigma,margin=192):
    """Equal weight per camera plane after normalization by mean core variance."""
    if prediction.shape!=target.shape or sigma.shape!=target.shape or margin<0:
        raise ValueError('Matching training tensors required')
    sl=(...,slice(margin,-margin),slice(margin,-margin)) if margin else (...,slice(None),slice(None))
    error=(prediction[sl]-target[sl]).square().mean(dim=(-2,-1))
    variance=sigma[sl].square().mean(dim=(-2,-1)).clamp_min(1e-8)
    return (error/variance).mean()
