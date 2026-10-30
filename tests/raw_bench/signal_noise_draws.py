"""Alternative distributions with identical CFA signal mean/variance.

Scaled Poisson is a sensitivity witness, NOT a calibrated electron model.
An empirical variance slope alone cannot identify sensor photon statistics.
"""
import numpy as np
from audit_signal_noise_bridge import PHASES


def normalized_draws(rng,target,slopes,gains,divisor,distribution):
    if (target.shape!=(256,256) or not np.isfinite(target).all() or np.min(target)<0
        or not np.isfinite(divisor) or divisor<=0):
        raise ValueError('Invalid latent signal')
    if distribution=='gaussian':return rng.normal(size=target.shape)
    if distribution!='scaled_poisson':raise ValueError('Unknown signal distribution')
    result=np.zeros_like(target,dtype=float)
    for phase,(dy,dx,_) in PHASES.items():
        slope,gain=slopes[phase],gains[phase]
        if not np.isfinite(slope) or slope<=0 or not np.isfinite(gain) or gain<=0:
            raise ValueError('Scaled Poisson requires positive slope and gain')
        rate=target[dy::2,dx::2]*divisor/(gain*slope)
        sample=rng.poisson(rate)
        np.divide(sample-rate,np.sqrt(rate),out=result[dy::2,dx::2],where=rate>0)
    return result
