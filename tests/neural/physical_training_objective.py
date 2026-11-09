"""Explicit bench objectives in signed native CFA units."""
import math
import torch


def training_loss(prediction, target, sigma, objective, sigma_floor, objective_scale=1.):
    if prediction.shape!=target.shape or sigma.shape!=target.shape:
        raise ValueError('Matching prediction, target and noisy-input sigma required')
    if not math.isfinite(sigma_floor) or sigma_floor<=0:
        raise ValueError('Positive finite sigma floor required')
    if not math.isfinite(objective_scale) or objective_scale<=0:
        raise ValueError('Positive finite overall objective scale required')
    if (not torch.isfinite(prediction).all() or not torch.isfinite(target).all()
            or not torch.isfinite(sigma).all() or (sigma<0).any()):
        raise ValueError('Finite observations and nonnegative sigma required')
    if objective=='unweighted L1':
        if objective_scale!=1.:
            raise ValueError('The unchanged L1 control must keep unit scale')
        return torch.nn.functional.l1_loss(prediction,target)
    if objective=='sigma_normalized_l1':
        return objective_scale*((prediction-target).abs()/sigma.detach().clamp_min(sigma_floor)).mean()
    raise ValueError('Unknown photographic training objective')


def fixed_sigma_scale(inverse_sigma_sum, sample_count):
    """One fixed harmonic-mean sigma: training calibration average weight is1."""
    if (not math.isfinite(inverse_sigma_sum) or inverse_sigma_sum<=0
            or not isinstance(sample_count,int) or isinstance(sample_count,bool) or sample_count<=0):
        raise ValueError('Positive finite calibration sum and integer sample count required')
    return sample_count/inverse_sigma_sum


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
