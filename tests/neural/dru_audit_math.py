"""Read-only loss diagnostics, fixed blur witnesses and declared recipe metadata."""
import math
import torch
from torch.nn import functional as F
from fine_texture_training import texture


FAMILIES=('texture_achromatic','texture_chromatic','constant','ramp','edge','wave_achromatic','wave_chromatic','edge')


def recipe(serial,seed,scenes):
    identity=280000000+seed*100000+serial;kind=serial%8
    row=dict(serial=serial,identity=identity,family=FAMILIES[kind],phase_index=(serial//8+serial)%4,
        scenario_index=None if serial%127==0 else (serial//32+serial//4)%len(scenes))
    if kind<2:
        _,metadata=texture(('weak_achromatic_wave','chromatic_wave')[kind],identity,128)
        row['texture']=metadata
    return row


def fixed_blurs(values):
    averaged=F.avg_pool2d(values,3,stride=1,padding=1)
    return {str(a):values+a*(averaged-values) for a in (.25,.5,1.)}


def loss_probe(values,target,sigma,margin):
    sl=(...,slice(margin,-margin),slice(margin,-margin)) if margin else (...,slice(None),slice(None))
    variance=sigma[sl].square().mean(dim=(-2,-1)).clamp_min(1e-8)
    output={}
    for name,prediction in dict(identity=values,**fixed_blurs(values)).items():
        error=prediction[sl]-target[sl]
        output[name]=dict(noise_normalized_mse=float((error.square().mean(dim=(-2,-1))/variance).mean()),
            raw_mse=float(error.square().mean()),plane_mse=error.square().mean(dim=(-2,-1)).flatten().tolist())
    # dL/dresidual = -2*sigma*error/(4*H*W*variance). Factor2/(4HW) removed below.
    normalized_gradient=sigma[sl]*(values[sl]-target[sl])/variance[:,:,None,None]
    return dict(losses=output,normalization_denominator=variance.flatten().tolist(),
        standardized_residual_gradient_rms_without_pixel_factor=float(normalized_gradient.square().mean().sqrt()))


def sigma_at_signal(scene,signal):
    rows=[]
    for p in sorted(scene['parameters'],key=lambda p:p['colour']):
        ratio=p['gain']/p['divisor'];a=p['a']*ratio;b=p['b']*ratio**2
        rows.append(dict(colour=p['colour'],a=a,b=b,sigma=math.sqrt(a*max(signal,0)+b)))
    return rows
