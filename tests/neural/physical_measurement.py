"""Signed physical effects with simultaneous Monte Carlo uncertainty.

Practical bounds concern systematic effects in declared ISO/plane groups,
not absolute errors of individual noisy periodograms.
"""
import math
import numpy as np

METRICS=('log_variance','lag_x1','lag_y1','log_psd_ratio')
BOUNDS=np.array([math.log(1.05),.02,.02,math.log(1.10)])


def vectors(delta):
    if delta.ndim==2:delta=delta[None]
    if delta.ndim!=3 or min(delta.shape[-2:])<16 or not np.isfinite(delta).all():
        raise ValueError('Finite replicate,H,W differences required')
    centered=delta-delta.mean(axis=(-2,-1),keepdims=True)
    power=np.mean(centered**2,axis=(-2,-1))
    variance=np.var(delta,axis=(-2,-1),ddof=1)/2
    h,w=delta.shape[-2:];window=np.outer(np.hanning(h),np.hanning(w))
    psd=np.abs(np.fft.fft2(centered*window,axes=(-2,-1)))**2
    fy,fx=np.meshgrid(np.fft.fftfreq(h),np.fft.fftfreq(w),indexing='ij');radius=np.sqrt(fx*fx+fy*fy)
    low=psd[:,(radius>0)&(radius<.1)].mean(axis=-1)
    high=psd[:,(radius>=.25)&(radius<=.5)].mean(axis=-1)
    if min(variance.min(),power.min(),low.min(),high.min())<=0:raise ValueError('Positive noise power required')
    return np.stack((np.log(variance),np.mean(centered[:,:,:-1]*centered[:,:,1:],axis=(-2,-1))/power,
                     np.mean(centered[:,:-1,:]*centered[:,1:,:],axis=(-2,-1))/power,np.log(low/high)),axis=-1)


def simultaneous(calibration,observed,quantile=.95):
    if calibration.ndim!=3 or observed.shape!=calibration.shape[1:] or calibration.shape[-1]!=4 or len(calibration)<32:
        raise ValueError('At least32 MC replicates,groups,4 metrics required')
    if not np.isfinite(calibration).all() or not np.isfinite(observed).all():raise ValueError('Finite measurement vectors required')
    center=calibration.mean(axis=0);scale=calibration.std(axis=0,ddof=1)
    if (scale<=0).any():raise ValueError('Nonzero MC uncertainty required')
    score=np.max(np.abs((calibration-center)/scale),axis=(1,2))
    critical=float(np.quantile(score,quantile))
    half_width=critical*scale*np.sqrt(1+1/len(calibration))
    effect=observed-center
    low,high=effect-half_width,effect+half_width
    passed=(low>=-BOUNDS)&(high<=BOUNDS)
    rejected=(low>BOUNDS)|(high<-BOUNDS)
    status='equivalent_within_declared_bounds' if passed.all() else ('outside_bounds' if rejected.any() else 'inconclusive')
    return dict(status=status,critical=critical,center=center.tolist(),half_width=half_width.tolist(),
                effect=effect.tolist(),low=low.tolist(),high=high.tolist(),passed=passed.tolist(),rejected=rejected.tolist(),
                practical_bounds=BOUNDS.tolist(),metrics=list(METRICS))


def classify(value,measurement):
    center=np.asarray(measurement['center']);half=np.asarray(measurement['half_width'])
    effect=np.asarray(value)-center
    if ((effect-half>BOUNDS)|(effect+half<-BOUNDS)).any():return 'outside_bounds'
    if ((effect-half>=-BOUNDS)&(effect+half<=BOUNDS)).all():return 'equivalent_within_declared_bounds'
    return 'inconclusive'


def binomial_upper_tail(count,total,probability=.05):
    return float(sum(math.comb(total,k)*probability**k*(1-probability)**(total-k) for k in range(count,total+1)))
