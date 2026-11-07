"""Composite-only data intervention; existing texture, noise and other slots stay fixed."""
import math
import torch
from context_data import extend_texture


def background_case(identity,kind,centre=64.):
    if kind not in ('flat','edge','coarse'):raise ValueError('Declared background required')
    values=torch.rand(4,generator=torch.Generator().manual_seed(identity+650000000)).tolist()
    return dict(kind=kind,angle=math.pi*values[0],phase=2*math.pi*values[1],
        amplitude=(.16+.20*values[2]) if kind=='edge' else (.04+.08*values[2]),
        frequency=.6+1.4*values[3],centre=centre)


def background(case,side,origin):
    axis=torch.arange(origin,origin+side,dtype=torch.float32)
    y,x=torch.meshgrid(axis,axis,indexing='ij')
    direction=(x-case['centre'])*math.cos(case['angle'])+(y-case['centre'])*math.sin(case['angle'])
    if case['kind']=='flat':unit=torch.zeros_like(x)
    elif case['kind']=='edge':unit=(direction>0).float()-.5
    elif case['kind']=='coarse':unit=torch.sin(direction*(2*math.pi*case['frequency']/128)+case['phase'])
    else:raise ValueError('Unknown background')
    return case['amplitude']*unit,unit


def training_texture(family,identity,side,serial):
    truth=extend_texture(family,identity,side)
    index=(serial//8)*2+serial%8;kind=('flat','edge','coarse')[index%3]
    if kind=='flat':return truth
    bg,_=background(background_case(identity,kind),side,-(side-128)//2)
    return truth+bg[None,None]


def reserved_signal(case,side=192,origin=0):
    axis=torch.arange(origin,origin+side,dtype=torch.float32)
    y,x=torch.meshgrid(axis,axis,indexing='ij')
    theta=(x*math.cos(case['angle'])+y*math.sin(case['angle']))*(2*math.pi*case['frequency']/128)+case['phase']
    wave=theta.sin();cosine=theta.cos()
    weights=torch.tensor(case['colours'])[:,None,None]
    bg,unit=background(case['background'],side,origin)
    truth=(.3+case['amplitude']*weights*wave+bg[None])[None]
    return truth,wave,cosine,unit
