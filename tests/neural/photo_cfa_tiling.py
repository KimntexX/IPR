"""Bench-only context-preserving CFA inference, including odd image extents."""
import numpy as np
import torch
from torch.nn import functional as F

from bayer_canonical import canonicalize,spatial_reflect


def orient_image(image,orientation):
    if orientation not in range(8):
        raise ValueError('Declared decoder orientation bits required')
    if orientation&2:image=image[::-1]
    if orientation&1:image=image[:,::-1]
    if orientation&4:image=image.swapaxes(0,1)
    return image


def correct_window(model,windows,pattern,sigma,device):
    if (windows.ndim!=3 or windows.dtype!=np.float32 or any(v%8 for v in windows.shape[-2:])
            or not np.isfinite(windows).all() or isinstance(sigma,bool) or not np.isfinite(sigma) or sigma<0):
        raise ValueError('Finite float32 windows,8pixel grid and nonnegative supplied sigma required')
    values=torch.from_numpy(np.ascontiguousarray(windows[:,None])).to(device)/2
    packed=F.pixel_unshuffle(values,2)
    labels=torch.tensor(np.asarray(pattern).reshape(1,4),dtype=torch.int64,device=device).expand(len(values),-1)
    deviations=torch.full_like(packed,sigma/2)
    packed,deviations,labels,flags=canonicalize(packed,deviations,labels)
    corrected=model.correct_observations(packed,deviations,labels)
    restored=spatial_reflect(F.pixel_shuffle(corrected,2),flags)*2
    return restored[:,0].cpu().numpy()


def tiled_correct(model,mosaic,pattern,sigma,destination,device='cpu',core=256,halo=128,batch=4,check=lambda:None,progress=lambda x,y:None):
    if (mosaic.ndim!=2 or mosaic.dtype!=np.float32 or destination.shape!=mosaic.shape
            or destination.dtype!=np.float32 or min(mosaic.shape)<2
            or isinstance(core,bool) or core<=0 or core%8 or halo<112 or halo%8 or batch<1):
        raise ValueError('Matching float32 CFA/output,8pixel grid and qualified112+ halo required')
    height,width=mosaic.shape
    extra_y=(-height)%core
    extra_x=(-width)%core
    # Reflecting a scalar CFA preserves its 2x2 phase (period2*(extent-1) is even).
    padded=np.pad(mosaic,((halo,halo+extra_y),(halo,halo+extra_x)),mode='reflect')
    positions=[(y,x) for y in range(0,height,core) for x in range(0,width,core)]
    with torch.inference_mode():
        for start in range(0,len(positions),batch):
            check()
            group=positions[start:start+batch]
            windows=np.stack([padded[y:y+core+2*halo,x:x+core+2*halo] for y,x in group])
            corrected=correct_window(model,windows,pattern,sigma,device)
            for image,(y,x) in zip(corrected,group):
                h,w=min(core,height-y),min(core,width-x)
                destination[y:y+h,x:x+w]=image[halo:halo+h,halo:halo+w]
            progress(min(start+batch,len(positions)),len(positions))
    return dict(tiles=len(positions),core=core,halo=halo,batch=batch,virtual_extension=[extra_y,extra_x],
        scope='Sensor-frame CFA, original extent preserved; virtual reflection only outside image, no resampling.')
