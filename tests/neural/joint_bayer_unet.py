"""Bench-only residual U-Net: signed balanced Bayer planes -> full-resolution RGB.

Spatial TL/TR/BL/BR inputs and explicit Bayer colour tags, not assumed RGGB.
Own random weights; fixed normalized-convolution baseline; no production hook.
"""
import torch
from torch import nn
from torch.nn import functional as F

from camera_unet import block


def validate(packed,sigma,pattern):
    if (packed.ndim!=4 or packed.shape[0]<1 or packed.shape[1]!=4 or min(packed.shape[2:])<8
            or packed.dtype!=torch.float32 or sigma.shape!=packed.shape or sigma.dtype!=packed.dtype
            or sigma.device!=packed.device or not torch.isfinite(packed).all()
            or not torch.isfinite(sigma).all() or (sigma<0).any()):
        raise ValueError('Finite N4HW float32 mosaic/sigma, minimum packed extent8 required')
    if (pattern.shape!=(packed.shape[0],4) or pattern.device!=packed.device
            or pattern.dtype!=torch.int64 or ((pattern<0)|(pattern>3)).any()
            or ((pattern==0).sum(dim=1)!=1).any() or ((pattern==2).sum(dim=1)!=1).any()):
        raise ValueError('One red, one blue and two green sites in N4 int64 pattern required')


def colour_tags(pattern):
    merged=torch.where(pattern==3,1,pattern)
    return F.one_hot(merged,num_classes=3).permute(0,2,1).float()


def bilinear(packed,pattern):
    validate(packed,torch.zeros_like(packed),pattern)
    n,_,h,w=packed.shape
    tags=colour_tags(pattern)[:,:,:,None,None]
    observed=F.pixel_shuffle((tags*packed[:,None]).reshape(n,12,h,w),2)
    support=F.pixel_shuffle(tags.expand(n,3,4,h,w).reshape(n,12,h,w),2)
    kernel=packed.new_tensor([[1.,2.,1.],[2.,4.,2.],[1.,2.,1.]])[None,None].expand(3,1,3,3)
    weights=F.conv2d(support,kernel,padding=1,groups=3)
    values=F.conv2d(observed,kernel,padding=1,groups=3)
    if (weights<=0).any():raise ValueError('No colour support for reconstruction')
    return torch.where(support>0,observed,values/weights)


class JointBayerUNet(nn.Module):
    def __init__(self,width=8):
        super().__init__()
        if isinstance(width,bool) or not isinstance(width,int) or not 4<=width<=64:
            raise ValueError('Integer width4..64 required')
        self.width=width
        self.enc1=block(20,width)
        self.enc2=block(width,2*width)
        self.middle=block(2*width,4*width)
        self.dec2=block(6*width,2*width)
        self.dec1=block(3*width,width)
        self.head=nn.Conv2d(width,12,1)
        nn.init.zeros_(self.head.weight);nn.init.zeros_(self.head.bias)

    def forward(self,packed,sigma,pattern):
        validate(packed,sigma,pattern)
        tags=colour_tags(pattern).reshape(len(packed),12,1,1).expand(-1,-1,*packed.shape[2:])
        a=self.enc1(torch.cat((packed,sigma,tags),dim=1))
        b=self.enc2(F.avg_pool2d(a,2));c=self.middle(F.avg_pool2d(b,2))
        d=self.dec2(torch.cat((F.interpolate(c,size=b.shape[2:],mode='nearest'),b),dim=1))
        e=self.dec1(torch.cat((F.interpolate(d,size=a.shape[2:],mode='nearest'),a),dim=1))
        return bilinear(packed,pattern)+F.pixel_shuffle(self.head(e),2)


def sample_rgb(rgb,pattern):
    if rgb.ndim!=4 or rgb.shape[1]!=3 or any(v%2 for v in rgb.shape[2:]):
        raise ValueError('Even-sized N3HW RGB required')
    packed=torch.stack([rgb[:,:,y::2,x::2] for y,x in ((0,0),(0,1),(1,0),(1,1))],dim=2)
    tags=colour_tags(pattern)[:,:,:,None,None]
    result=(packed*tags).sum(dim=1)
    validate(result,torch.zeros_like(result),pattern)
    return result
