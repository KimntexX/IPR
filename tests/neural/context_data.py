"""Analytic halos around immutable historical centres, not reflected copies."""
import math

import torch

from fine_texture_training import texture
from run_diverse_moment_pilot import edge_image
from synthetic_bayer_data import signal, noisy_observation


def margin(side, base_side):
    if (isinstance(side, bool) or not isinstance(side, int) or side <= base_side
            or (side-base_side) % 16):
        raise ValueError('Larger context with centred8pixel-aligned origin required')
    return (side-base_side)//2


def normalized_axis(side):
    offset = margin(side, 128)
    centre = torch.linspace(-1, 1, 128)
    before = -1-torch.arange(offset, 0, -1, dtype=torch.float32)*(2/127)
    after = 1+torch.arange(1, offset+1, dtype=torch.float32)*(2/127)
    return torch.cat((before, centre, after))


def checked_centre(extended, centre):
    offset = margin(extended.shape[-1], centre.shape[-1])
    if not torch.equal(extended[:, :, offset:-offset, offset:-offset], centre):
        raise ValueError('Analytic halo changed the immutable historical centre')
    return extended


def extend_signal(family, seed, side=256):
    params = torch.rand(8, generator=torch.Generator().manual_seed(seed))
    y, x = torch.meshgrid(normalized_axis(side), normalized_axis(side), indexing='ij')
    direction = x*torch.cos(params[0]*math.pi)+y*torch.sin(params[0]*math.pi)
    base = .05+.65*params[1:4, None, None]
    if family == 'constant':
        image = base.expand(3, side, side).clone()-.08
    elif family == 'ramp':
        image = base+direction[None]*(.1+.35*params[4:7, None, None])
    elif family == 'edge':
        weight = (direction > .4*(params[7]-.5)).float()[None]
        other = .08+.8*params[4:7, None, None]
        image = base*(1-weight)+other*weight
    elif family in ('weak_achromatic_wave', 'chromatic_wave'):
        phase = 2*math.pi*(2.1+.9*params[4])*direction+params[5]*2*math.pi
        image = (base+.015*torch.sin(phase)[None] if family == 'weak_achromatic_wave'
            else base+.12*torch.stack([torch.sin(phase+i*2*math.pi/3) for i in range(3)]))
    else:
        raise ValueError('Known procedural family required')
    return checked_centre(image.float()[None], signal(family, seed, 128)[None])


def extend_edge(recipe, side=256):
    centre = edge_image(recipe, 128)
    y, x = torch.meshgrid(normalized_axis(side), normalized_axis(side), indexing='ij')
    angle = recipe['normal_degrees']*math.pi/180
    mask = (x*math.cos(angle)+y*math.sin(angle) > recipe['offset'])[None, None]
    low, high = (torch.tensor(recipe[n])[None, :, None, None] for n in ('low_rgb', 'high_rgb'))
    return checked_centre(torch.where(mask, high, low).float(), centre)


def extend_texture(family, serial, side=256):
    centre, data = texture(family, serial, 128)
    offset = margin(side, 128)
    axis = torch.arange(-offset, 128+offset, dtype=torch.float32)
    y, x = torch.meshgrid(axis, axis, indexing='ij')
    wave = ((x*math.cos(data['direction'])+y*math.sin(data['direction']))
        *(2*math.pi*data['frequency']/128)+data['phase_radians']).sin()
    second = ((x*math.cos(data['other_direction'])+y*math.sin(data['other_direction']))
        *(2*math.pi*data['other_frequency']/128)+data['other_phase_radians']).sin()
    wave = (wave+.35*second)/1.35
    image = (data['dc']+data['amplitude']*torch.tensor(data['colours'])[:, None, None]*wave)[None]
    return checked_centre(image, centre)


def extend_case(case, side):
    if 'family' in case:
        return extend_signal(case['family'], case['seed'], side)
    return extend_edge(case, side)


def crop_centre(image, side=128):
    offset = margin(image.shape[-1], side)
    return image[:, :, offset:-offset, offset:-offset]


def extend_observation(truth, pattern, original, original_sigma, a, b, halo_seed):
    if (truth.ndim != 4 or truth.shape[0:2] != (1, 3) or truth.shape[-1] != truth.shape[-2]
            or original.ndim != 4 or original.shape[0:2] != (1, 4)
            or original.shape != original_sigma.shape or original.shape[-1] != original.shape[-2]
            or original.dtype != torch.float32 or original_sigma.dtype != torch.float32
            or original.device != truth.device or original_sigma.device != truth.device
            or not torch.isfinite(original).all() or not torch.isfinite(original_sigma).all()
            or (original_sigma < 0).any() or isinstance(halo_seed, bool)
            or not isinstance(halo_seed, int) or halo_seed < 0):
        raise ValueError('Finite square RGB context and immutable CFA/sigma centres required')
    offset = margin(truth.shape[-1], 2*original.shape[-1])//2
    observed, sigma = noisy_observation(truth, pattern, a, b, torch.Generator().manual_seed(halo_seed))
    observed[:, :, offset:-offset, offset:-offset] = original
    sigma[:, :, offset:-offset, offset:-offset] = original_sigma
    return observed, sigma
