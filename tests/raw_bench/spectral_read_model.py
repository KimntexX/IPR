"""Research stationary CFA read-noise ensemble, preserving phase cross spectra.

Spectral shape from three development darks; amplitude from frozen temporal b.
Gaussian/stationary assumption, not identified sensor FPN or physical truth.
"""
import numpy as np
from audit_signal_noise_bridge import PHASES


def prepare(reads,context):
    if len(reads)<3:raise ValueError('At least three explicit calibration tiles required; caller must enforce physical frame count')
    fields=[]
    for read in reads:
        if read.shape!=(256,256) or not np.isfinite(read).all():raise ValueError('Invalid calibration read')
        phases=np.stack([read[dy::2,dx::2] for dy,dx,_ in PHASES.values()],axis=-1)
        phases=phases-phases.mean(axis=(0,1),keepdims=True)
        fields.append(np.fft.fft2(phases,axes=(0,1),norm='ortho'))
    spectral=np.mean([f[..., :,None]*f[...,None,:].conj() for f in fields],axis=0)
    observed=np.real(np.diagonal(spectral,axis1=-2,axis2=-1)).mean(axis=(0,1))
    target=np.array([context['phases'][phase]['b'] for phase in PHASES])
    if np.min(observed)<=0 or np.min(target)<=0:raise ValueError('Degenerate read spectrum')
    scale=np.sqrt(target/observed)
    spectral=spectral*scale[None,None,:,None]*scale[None,None,None,:]
    spectral=(spectral+spectral.swapaxes(-1,-2).conj())/2
    eigenvalues,vectors=np.linalg.eigh(spectral)
    tolerance=max(float(np.abs(eigenvalues).max()),1e-30)*1e-10
    if eigenvalues.min()<-tolerance:raise ValueError('Non-PSD empirical spectrum')
    root=(vectors*np.sqrt(np.maximum(eigenvalues,0))[...,None,:])@vectors.swapaxes(-1,-2).conj()
    return dict(root=root,summary=dict(calibration_tiles=len(reads),spatial_variance_dn2=observed.tolist(),
        target_temporal_variance_dn2=target.tolist(),phase_scale=scale.tolist(),min_eigenvalue=float(eigenvalues.min()),
        numerical_negative_eigenvalues=int(np.sum(eigenvalues<0)),PSD_roundoff_tolerance=tolerance,
        dc='Per-frame per-phase constant removed; no model for black-offset drift.',
        scope='Gaussian stationary spectral shape, same session, not a physical fixed-pattern identification.'))


def draw(model,seed,iso):
    rng=np.random.default_rng(np.random.SeedSequence([seed,iso,44291]))
    white=np.fft.fft2(rng.normal(size=(128,128,4)),axes=(0,1),norm='ortho')
    colored=np.einsum('...ij,...j->...i',model['root'],white)
    phases=np.fft.ifft2(colored,axes=(0,1),norm='ortho')
    if np.max(np.abs(phases.imag))>1e-8*max(float(np.max(np.abs(phases.real))),1e-30):raise ValueError('Spectrum lost real-field symmetry')
    mosaic=np.empty((256,256))
    for index,(_, (dy,dx,_)) in enumerate(PHASES.items()):mosaic[dy::2,dx::2]=phases.real[...,index]
    return mosaic
