"""Unqualified sensor-law interpolation and balanced-camera deviation conversion."""
import numpy as np


def interpolate_noise(fits,iso,plane):
    def at(nodes):
        nodes=sorted(nodes)
        x=np.array([r[0] for r in nodes]);y=np.array([r[1] for r in nodes])
        if not len(nodes) or not np.isfinite([*x,*y,iso]).all() or (x<=0).any() or (y<=0).any() or not x[0]<=iso<=x[-1]:
            raise ValueError('Positive witnessed ISO interpolation support required; no extrapolation')
        return float(np.exp(np.interp(np.log(iso),np.log(x),np.log(y))))
    a=at([(r['iso'],r['fit']['shot_slope_dn']) for r in fits if r['plane']==plane and 'shot_slope_dn' in r.get('fit',{})])
    b=at([(r['iso'],r['reference_dark_variance_dn2']) for r in fits if r['plane']==plane and r.get('reference_dark_variance_dn2',0)>0])
    return a,b


def balanced_sigma(signal_dn,a,b,gain,divisor):
    if (not np.isfinite([a,b,gain,divisor]).all() or min(a,b)<0 or min(gain,divisor)<=0
            or not np.isfinite(signal_dn).all()):raise ValueError('Finite signal and nonnegative variance law required')
    return (np.sqrt(a*np.maximum(signal_dn,0)+b)*gain/divisor).astype(np.float32)
