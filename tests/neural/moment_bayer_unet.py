"""Bench interpolation with constant and first-moment reproduction constraints.

Each same-colour interpolant has mass1/first-moment0. Its difference
from bilinear has mass0/first-moment0. Bounded learned coefficients cannot
create a constant-field correction. Affine guarantee excludes reflected edges.
No noisy-observation support or production hook in this qualification stage.
"""
import torch
from torch import nn
from torch.nn import functional as F

from camera_unet import block
from joint_bayer_unet import JointBayerUNet,validate,bilinear,sparse_rgb,colour_tags


def stencils():
    result=[]
    for colour in range(3):
        for y,x in ((0,0),(0,1),(1,0),(1,1)):
            observed=(colour==0 and (y,x)==(0,0)) or (colour==2 and (y,x)==(1,1)) or (colour==1 and y!=x)
            if observed:continue
            if colour==1:vectors=((0,1),(1,0))
            else:
                sy,sx=(0,0) if colour==0 else (1,1)
                vectors={(0,1):((0,1),(2,1)),(1,0):((1,0),(1,2)),
                         (1,1):((1,1),(1,-1))}[((sy-y)%2,(sx-x)%2)]
            dy,dx=vectors[0]
            supports=tuple(((a,b,.5),(-a,-b,.5)) for a,b in vectors)
            # One-sided affine extrapolation lets an edge use only one side,
            # without sacrificing mass1 and first-moment0.
            supports+=(((dy,dx,1.5),(3*dy,3*dx,-.5)),((-dy,-dx,1.5),(-3*dy,-3*dx,-.5)))
            result.append((colour,y,x,supports))
    return tuple(result)


STENCILS=stencils()


def interpolate_weights(packed,pattern,coefficients):
    validate(packed,torch.zeros_like(packed),pattern)
    if (pattern[:,0]!=0).any() or (pattern[:,3]!=2).any():raise ValueError('Canonical red-TL blue-BR input required')
    n,_,h,w=packed.shape
    if (coefficients.shape!=(n,8,4,h,w) or coefficients.dtype!=packed.dtype
            or coefficients.device!=packed.device or not torch.isfinite(coefficients).all()):
        raise ValueError('Finite N8x4xHxW coefficient field required')
    if (coefficients.abs()>1).any():raise ValueError('Bounded coefficients in [-1,1] required')
    base=bilinear(packed,pattern);observed,_=sparse_rgb(packed,pattern)
    padded=F.pad(observed,(3,3,3,3),mode='reflect')
    plane=[[base[:,c,y::2,x::2] for y,x in ((0,0),(0,1),(1,0),(1,1))] for c in range(3)]
    for i,(colour,y,x,supports) in enumerate(STENCILS):
        candidates=[]
        for support in supports:
            values=[padded[:,colour,3+dy:3+dy+2*h,3+dx:3+dx+2*w][:,y::2,x::2]
                for dy,dx,_ in support]
            a,b=values
            candidates.append(a*.5+b*.5 if support[0][2]==.5 else a+(a-b)*.5)
        centre=plane[colour][2*y+x]
        difference=torch.stack(candidates,dim=1)-centre[:,None]
        plane[colour][2*y+x]=centre+(coefficients[:,i]*difference).sum(dim=1)
    output=F.pixel_shuffle(torch.stack([p for colours in plane for p in colours],dim=1),2)
    if not torch.isfinite(output).all():raise ValueError('Interpolation overflow')
    return output


class MomentBayerUNet(nn.Module):
    def __init__(self,width=8):
        super().__init__()
        if isinstance(width,bool) or not isinstance(width,int) or not 4<=width<=64:
            raise ValueError('Integer width4..64 required')
        self.width=width
        self.enc1=block(20,width);self.enc2=block(width,2*width);self.middle=block(2*width,4*width)
        self.dec2=block(6*width,2*width);self.dec1=block(3*width,width)
        self.head=nn.Conv2d(width,32,1)
        nn.init.zeros_(self.head.weight);nn.init.zeros_(self.head.bias)

    def forward(self,packed,sigma,pattern,exact_observations=False):
        validate(packed,sigma,pattern)
        if exact_observations is not True or (sigma!=0).any():
            raise ValueError('Moment reconstruction currently supports declared noiseless observations only')
        tags=colour_tags(pattern).reshape(len(packed),12,1,1).expand(-1,-1,*packed.shape[2:])
        a=self.enc1(torch.cat((packed,sigma,tags),dim=1));b=self.enc2(F.avg_pool2d(a,2))
        c=self.middle(F.avg_pool2d(b,2))
        d=self.dec2(torch.cat((F.interpolate(c,size=b.shape[2:],mode='nearest'),b),dim=1))
        e=self.dec1(torch.cat((F.interpolate(d,size=a.shape[2:],mode='nearest'),a),dim=1))
        coefficients=torch.tanh(self.head(e)).reshape(len(packed),8,4,*packed.shape[2:])
        return interpolate_weights(packed,pattern,coefficients)


def model_from_config(config):
    kind=config.get('model_kind','residual')
    if kind=='residual':return JointBayerUNet(config['width'])
    if kind=='moment':return MomentBayerUNet(config['width'])
    raise ValueError('Unknown Bayer model kind')
