"""Known synthetic variance for a training loss, never an inference input."""
import math
import torch


def clean_sigma(target,labels,scenario):
    """Target is canonical packed camera /2; scenario law is balanced camera."""
    if (target.ndim!=4 or target.shape[:2]!=(1,4) or labels.shape!=(1,4)
            or sorted(labels[0].tolist())!=[0,1,2,3] or not torch.isfinite(target).all()):
        raise ValueError('One finite four-plane clean target with distinct labels required')
    if scenario is None:return torch.zeros_like(target)
    parameters=scenario['parameters'];by_colour={p['colour']:p for p in parameters}
    if len(parameters)!=4 or set(by_colour)!={0,1,2,3}:raise ValueError('Four distinct sensor laws required')
    aa=[];bb=[]
    for colour in labels[0].tolist():
        p=by_colour[colour]
        if not all(math.isfinite(p[k]) for k in ('a','b','gain','divisor')) or min(p['a'],p['b'])<0 or min(p['gain'],p['divisor'])<=0:
            raise ValueError('Finite physical noise coefficients required')
        ratio=p['gain']/p['divisor'];aa.append(p['a']*ratio/2);bb.append(p['b']*ratio**2/4)
    a=target.new_tensor(aa).reshape(1,4,1,1);b=target.new_tensor(bb).reshape(1,4,1,1)
    variance=a*target.detach().clamp_min(0)+b
    if not torch.isfinite(variance).all():raise ValueError('Variance overflow')
    return variance.sqrt()
