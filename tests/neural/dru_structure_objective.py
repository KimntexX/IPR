"""Current training losses and global-projection diagnostic witness; no inference hook."""
import math
import torch
from torch.nn import functional as F
from dru_training_data import core_loss
from dru_haar_objective import noise_aware_haar_penalty


def structure_penalty(prediction,target,margin=192):
    if prediction.shape!=target.shape or prediction.ndim!=4 or margin<0:
        raise ValueError('Matching image tensors and nonnegative margin required')
    if min(prediction.shape[-2:])-2*margin<8:raise ValueError('At least eight core cells required')
    sl=(...,slice(margin,-margin),slice(margin,-margin)) if margin else (...,slice(None),slice(None))
    predicted=prediction[sl];clean=target[sl].detach();penalties=[]
    for scale in (1,2,4):
        p=predicted if scale==1 else F.avg_pool2d(predicted,scale)
        t=clean if scale==1 else F.avg_pool2d(clean,scale)
        dx=t[:,:,:,1:]-t[:,:,:,:-1];dy=t[:,:,1:,:]-t[:,:,:-1,:]
        ex=(p[:,:,:,1:]-p[:,:,:,:-1])-dx
        ey=(p[:,:,1:,:]-p[:,:,:-1,:])-dy
        projection=(ex*dx).mean(dim=(-2,-1))+(ey*dy).mean(dim=(-2,-1))
        energy=dx.square().mean(dim=(-2,-1))+dy.square().mean(dim=(-2,-1))
        penalties.append((projection/(energy+1e-8)).square().mean())
    return torch.stack(penalties).mean()


def loss_components(prediction,target,sigma,objective,margin=192,known_sigma=None,weight=1.):
    if objective not in ('mse','haar_noise_aware_v1'):raise ValueError('Unknown training objective')
    if not math.isfinite(weight) or weight<=0:raise ValueError('Positive finite objective weight required')
    mse=core_loss(prediction,target,sigma,margin)
    if objective=='mse':return mse,mse,mse.new_zeros(())
    if known_sigma is None:raise ValueError('Known synthetic variance required for Haar training loss')
    structure=noise_aware_haar_penalty(prediction,target,known_sigma,margin)
    return mse+weight*structure,mse,structure
