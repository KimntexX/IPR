"""Explicit bench objectives in signed native CFA units."""
import math
import torch

# Objectives whose magnitude is already that of the unweighted L1 control: no calibrated scale.
UNIT_SCALE_OBJECTIVES = ('unweighted L1', 'unweighted L2', 'l1_plane_mean')


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
    if objective in UNIT_SCALE_OBJECTIVES and objective_scale!=1.:
        raise ValueError('Unit-scale objectives must keep unit scale')
    if objective=='unweighted L1':
        return torch.nn.functional.l1_loss(prediction,target)
    if objective=='unweighted L2':
        # Conditional mean instead of the L1 conditional median: no median bias under skewed noise.
        return torch.nn.functional.mse_loss(prediction,target)
    if objective=='l1_plane_mean':
        # L1 plus the absolute mean error of every plane of every sample: same per-pixel weight as the L1
        # term, but coherent across the plane, so it pulls the plane means onto the target's.
        error=prediction-target
        return error.abs().mean()+error.mean(dim=(-2,-1)).abs().mean()
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
