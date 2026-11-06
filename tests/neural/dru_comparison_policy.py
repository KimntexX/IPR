"""Predeclared single-factor data policies, no test targets or learned parameters."""
import math
import torch
from context_data import extend_texture
from fine_texture_training import texture,POLICY


ARMS=('reference','noise','details')


def texture_parameters(family,identity,serial):
    _,metadata=texture(family,identity,128)
    draws=torch.rand(12,generator=torch.Generator().manual_seed(POLICY['seed_base']+identity)).tolist()
    index=(serial//8)*2+serial%8
    frequency_bounds=(2.,8.,16.,28.);amplitude_bounds=(.003/1.35,.012,.04,.12/1.35)
    f=index//3%3;a=index%3
    frequency=math.exp(math.log(frequency_bounds[f])+draws[0]*math.log(frequency_bounds[f+1]/frequency_bounds[f]))
    amplitude=math.exp(math.log(amplitude_bounds[a])+draws[1]*math.log(amplitude_bounds[a+1]/amplitude_bounds[a]))*1.35
    secondary=max(2.,min(28.,frequency*metadata['other_frequency']/metadata['frequency']))
    return dict(metadata,frequency=frequency,amplitude=amplitude,other_frequency=secondary,
        frequency_bin=f,amplitude_bin=a)


def texture_context(family,identity,side,serial,arm):
    if arm not in ARMS:raise ValueError('Declared comparison arm required')
    if arm!='details':return extend_texture(family,identity,side)
    metadata=texture_parameters(family,identity,serial)
    frequency=metadata['frequency'];amplitude=metadata['amplitude'];secondary=metadata['other_frequency']
    offset=(side-128)//2;axis=torch.arange(-offset,128+offset,dtype=torch.float32)
    y,x=torch.meshgrid(axis,axis,indexing='ij')
    wave=((x*math.cos(metadata['direction'])+y*math.sin(metadata['direction']))*(2*math.pi*frequency/128)+metadata['phase_radians']).sin()
    other=((x*math.cos(metadata['other_direction'])+y*math.sin(metadata['other_direction']))*(2*math.pi*secondary/128)+metadata['other_phase_radians']).sin()
    wave=(wave+.35*other)/1.35
    return (metadata['dc']+amplitude*torch.tensor(metadata['colours'])[:,None,None]*wave)[None]


def scenario_for(serial,identity,scenarios,arm):
    if arm not in ARMS:raise ValueError('Declared comparison arm required')
    if serial%127==0:return None
    original=scenarios[(serial//32+serial//4)%len(scenarios)]
    if arm!='noise' or serial//32%2==0:return original
    draws=torch.rand(5,generator=torch.Generator().manual_seed(identity+70000000)).tolist()
    a=math.exp(math.log(1e-5)+draws[0]*math.log(4e-3/1e-5))
    b=math.exp(math.log(1e-6)+draws[1]*math.log(5e-4/1e-6))
    ratios=[1.]*3 if serial//64%4==0 else [math.exp(math.log(.7)+v*math.log(1.5/.7)) for v in draws[2:]]
    return dict(name='continuous_variance_surrogate',parameters=[dict(colour=c,a=a*ratios[k],b=b*ratios[k]**2,gain=1.,divisor=1.) for c,k in ((0,0),(1,1),(3,1),(2,2))])
