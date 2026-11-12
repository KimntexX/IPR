"""Signed residual distributions and same-plane spatial correlations.

Adapted from study_noise_residual_distributions, without a filter, whitening,
candidate thresholds or the assumption that every residual is a pair/sqrt(2).
Use existing neural residual_spectrum for frequency-band investigations.
"""
import numpy as np
from .contracts import field, mask_for


def describe_residual(residual, *, domain, unit, mask=None, lags=()):
    """CHW signed error supplied by caller; sample covariance (N-1 denominator).

    A pair difference must be explicitly divided by sqrt(2) by the caller,
    and is representative of individual noise only for independent equal
    variances. Constant-plane standardized moments/correlations are undefined.
    """
    a = field(residual, domain)
    if not isinstance(unit, str) or not unit.strip():
        raise ValueError('Explicit residual unit required')
    m = mask_for(a.shape[1:], mask)
    if m.sum() < 2:
        raise ValueError('At least two valid samples required')
    values = a[:, m]
    mean = values.mean(axis=1)
    centered = values-mean[:, None]
    cov = centered @ centered.T/(values.shape[1]-1)
    sigma = np.sqrt(np.diag(cov))
    moments = []
    for c, sd in enumerate(sigma):
        # Population moments give scale-independent skewness/kurtosis.
        variance = np.mean(centered[c]**2)
        moments.append(dict(skewness=float(np.mean(centered[c]**3)/variance**1.5) if sd > 0 else None,
                            excess_kurtosis=float(np.mean(centered[c]**4)/variance**2-3) if sd > 0 else None))
    correlations = []
    h, w = a.shape[1:]
    for lag in lags:
        if (len(lag) != 2 or any(isinstance(i, bool) or not isinstance(i, (int, np.integer)) for i in lag)
                or tuple(lag) == (0, 0) or abs(lag[0]) >= h or abs(lag[1]) >= w):
            raise ValueError('Nonzero integer dy,dx lag within spatial support required')
        dy, dx = lag
        y, x = slice(max(0, -dy), min(h, h-dy)), slice(max(0, -dx), min(w, w-dx))
        yy, xx = slice(max(0, dy), min(h, h+dy)), slice(max(0, dx), min(w, w+dx))
        valid = m[y, x] & m[yy, xx]
        left, right = a[:, y, x][:, valid], a[:, yy, xx][:, valid]
        scores = [None]*a.shape[0]
        if valid.sum() >= 2:
            left = left-left.mean(axis=1, keepdims=True)
            right = right-right.mean(axis=1, keepdims=True)
            den = np.sqrt(np.sum(left**2, axis=1)*np.sum(right**2, axis=1))
            scores = [float(np.clip(np.dot(l, r)/d, -1, 1)) if d > 0 else None
                      for l, r, d in zip(left, right, den)]
        correlations.append(dict(lag_dy_dx=list(lag), valid_pairs=int(valid.sum()), by_plane=scores))
    return dict(domain=domain, unit=unit, covariance_unit=f'({unit})^2',
                samples=int(m.sum()), mean=mean.tolist(), sigma=sigma.tolist(),
                covariance=cov.tolist(), moments=moments,
                quantile_probabilities=[.01, .5, .99],
                quantiles=np.quantile(values, [.01, .5, .99], axis=1).tolist(),
                negative_fraction=np.mean(values < 0, axis=1).tolist(),
                zero_fraction=np.mean(values == 0, axis=1).tolist(),
                lag_correlations=correlations)
