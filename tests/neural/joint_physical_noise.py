"""Joint Bayer-plane diagnostics in native packed coordinates, without resampling."""
import numpy as np

PLANE_PAIRS=((0,1),(0,2),(0,3),(1,2),(1,3),(2,3))


def correlations(differences):
    if differences.ndim == 3:
        differences=differences[None]
    if differences.ndim != 4 or differences.shape[1] != 4 or not np.isfinite(differences).all():
        raise ValueError('Finite replicate,4,H,W differences required')
    centered=differences-differences.mean(axis=(-2,-1),keepdims=True)
    power=np.mean(centered**2,axis=(-2,-1))
    if (power <= 0).any():
        raise ValueError('Positive power required in all four planes')
    return np.stack([np.mean(centered[:,i]*centered[:,j],axis=(-2,-1))/np.sqrt(power[:,i]*power[:,j])
                     for i,j in PLANE_PAIRS],axis=-1)
