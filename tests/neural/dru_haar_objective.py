"""Experimental training-only local Haar projection; no production registration."""
import torch


def haar_step(image):
    if image.ndim!=4 or image.shape[-2]%2 or image.shape[-1]%2:
        raise ValueError('Even image axes required for orthonormal Haar')
    a=image[:,:,0::2,0::2];b=image[:,:,0::2,1::2]
    c=image[:,:,1::2,0::2];d=image[:,:,1::2,1::2]
    return (a+b+c+d)/2,((a-b+c-d)/2,(a+b-c-d)/2,(a-b-c+d)/2)


def local_haar_penalty(prediction,target,margin=192):
    if (prediction.shape!=target.shape or prediction.ndim!=4 or margin<0
            or prediction.dtype!=target.dtype or prediction.device!=target.device):
        raise ValueError('Matching image tensors and nonnegative margin required')
    h,w=prediction.shape[-2:]
    if min(h,w)-2*margin<64 or (h-2*margin)%64 or (w-2*margin)%64:
        raise ValueError('Core axes must be multiples of 64 cells')
    sl=(...,slice(margin,-margin),slice(margin,-margin)) if margin else (...,slice(None),slice(None))
    p=prediction[sl];t=target[sl].detach();terms=[]
    for _ in range(3):
        p,p_bands=haar_step(p);t,t_bands=haar_step(t)
        for predicted,clean in zip(p_bands,t_bands):
            # Separate orientations, planes and 8x8 coefficient neighbourhoods.
            error=(predicted-clean).unfold(2,8,8).unfold(3,8,8)
            truth=clean.unfold(2,8,8).unfold(3,8,8)
            projection=(error*truth).mean(dim=(-2,-1))
            energy=truth.square().mean(dim=(-2,-1))
            terms.append((projection/(energy+1e-8)).square().mean())
    return torch.stack(terms).mean()


def noise_aware_haar_penalty(prediction,target,sigma,margin=192):
    """Projection squared divided by clean energy squared plus its noise variance.

    Noise propagation assumes independent zero-mean input CFA samples with the
    supplied per-sample variance. It does not model demosaic/LCP correlations.
    """
    if (prediction.shape!=target.shape or sigma.shape!=target.shape or prediction.ndim!=4
            or prediction.dtype!=target.dtype or sigma.dtype!=target.dtype
            or prediction.device!=target.device or sigma.device!=target.device or margin<0):
        raise ValueError('Matching prediction, clean target and sigma required')
    if not torch.isfinite(sigma).all() or (sigma<0).any():raise ValueError('Finite nonnegative sigma required')
    h,w=prediction.shape[-2:]
    if min(h,w)-2*margin<64 or (h-2*margin)%64 or (w-2*margin)%64:raise ValueError('Core axes must be multiples of 64')
    sl=(...,slice(margin,-margin),slice(margin,-margin)) if margin else (...,slice(None),slice(None))
    p=prediction[sl];t=target[sl].detach();variance=sigma[sl].detach().square();terms=[]
    for _ in range(3):
        p,p_bands=haar_step(p);t,t_bands=haar_step(t)
        # Each orthonormal 2x2 coefficient has weights +/-1/2.
        variance=(variance[:,:,0::2,0::2]+variance[:,:,0::2,1::2]+
            variance[:,:,1::2,0::2]+variance[:,:,1::2,1::2])/4
        tile_variance=variance.unfold(2,8,8).unfold(3,8,8)
        for predicted,clean in zip(p_bands,t_bands):
            error=(predicted-clean).unfold(2,8,8).unfold(3,8,8)
            truth=clean.unfold(2,8,8).unfold(3,8,8)
            projection=(error*truth).mean(dim=(-2,-1))
            energy=truth.square().mean(dim=(-2,-1))
            # Var(mean(error*truth)) = sum(var(error)*truth^2)/N^2, N=64.
            uncertainty=(tile_variance*truth.square()).mean(dim=(-2,-1))/64
            terms.append((projection.square()/((energy+1e-8).square()+uncertainty)).mean())
    return torch.stack(terms).mean()
