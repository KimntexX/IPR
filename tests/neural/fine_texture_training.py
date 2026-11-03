"""Deterministic bounded-memory procedural texture enrichment, not test targets."""
import math

import torch


POLICY = dict(version=1, seed_base=120000000, frequency_cycles_per_128=[2., 28.],
    amplitude=[.003, .12], dc=[-.02, .6], secondary_weight=.35,
    distribution='Log-uniform frequency/amplitude; uniform direction/phase/DC; two plane waves.',
    families=['weak_achromatic_wave', 'chromatic_wave'], clipping=False)


def texture(family, serial, side):
    if (family not in POLICY['families'] or isinstance(serial, bool) or not isinstance(serial, int)
            or serial < 0 or side != 128):
        raise ValueError('Declared texture family, nonnegative serial and side128 required')
    seed = POLICY['seed_base']+serial
    values = torch.rand(12, generator=torch.Generator().manual_seed(seed)).tolist()
    log_draw = lambda bounds, u: math.exp(math.log(bounds[0])+u*math.log(bounds[1]/bounds[0]))
    frequency = log_draw(POLICY['frequency_cycles_per_128'], values[0])
    amplitude = log_draw(POLICY['amplitude'], values[1])
    dc = -.02+.62*values[2]
    direction, phase = math.pi*values[3], 2*math.pi*values[4]
    other_frequency = max(2., min(28., frequency*(.5+values[5])))
    other_direction, other_phase = math.pi*values[6], 2*math.pi*values[7]
    y, x = torch.meshgrid(torch.arange(side, dtype=torch.float32),
        torch.arange(side, dtype=torch.float32), indexing='ij')
    wave = ((x*math.cos(direction)+y*math.sin(direction))*(2*math.pi*frequency/128)+phase).sin()
    second = ((x*math.cos(other_direction)+y*math.sin(other_direction))*(2*math.pi*other_frequency/128)+other_phase).sin()
    wave = (wave+.35*second)/1.35
    if family == 'weak_achromatic_wave':
        colours = torch.ones(3)
    else:
        colours = torch.tensor([2*v-1 for v in values[8:11]])
        colours /= colours.abs().max().clamp_min(1e-6)
    truth = (dc+amplitude*colours[:, None, None]*wave)[None]
    metadata = dict(seed=seed, frequency=frequency, amplitude=amplitude, dc=dc,
        direction=direction, phase=phase, other_frequency=other_frequency,
        other_direction=other_direction, other_phase=other_phase, colours=colours.tolist())
    return truth, metadata
