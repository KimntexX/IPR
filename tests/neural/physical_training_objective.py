"""Explicit bench objectives in signed native CFA units."""
import math
import torch


def training_loss(prediction, target, sigma, objective, sigma_floor):
    if prediction.shape!=target.shape or sigma.shape!=target.shape:
        raise ValueError('Matching prediction, target and noisy-input sigma required')
    if not math.isfinite(sigma_floor) or sigma_floor<=0:
        raise ValueError('Positive finite sigma floor required')
    if (not torch.isfinite(prediction).all() or not torch.isfinite(target).all()
            or not torch.isfinite(sigma).all() or (sigma<0).any()):
        raise ValueError('Finite observations and nonnegative sigma required')
    if objective=='unweighted L1':
        return torch.nn.functional.l1_loss(prediction,target)
    if objective=='sigma_normalized_l1':
        return ((prediction-target).abs()/sigma.detach().clamp_min(sigma_floor)).mean()
    raise ValueError('Unknown photographic training objective')


def verify_controls(actual, expected, tolerance=1e-6):
    """A continuation must reproduce the parent before the first update."""
    keys=lambda rows:{(r['target_index'],r['iso']):r for r in rows}
    a,b=keys(actual),keys(expected)
    if len(a)!=len(actual) or len(b)!=len(expected) or a.keys()!=b.keys():
        raise ValueError('Continuation control identities differ from the parent')
    errors=[]
    for key in a:
        for metric in ('corrected_mse','observed_mse'):
            x,y=a[key][metric],b[key][metric]
            if not math.isfinite(x) or not math.isfinite(y) or x<0 or y<=0:
                raise ValueError('Invalid continuation control measurement')
            errors.append(abs(x-y)/y)
    maximum=max(errors,default=float('inf'))
    if maximum>tolerance:
        raise ValueError('Continuation does not reproduce its frozen parent controls')
    return maximum
