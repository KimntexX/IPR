"""Bench-only CFA residual adapter; official DRUNet topology, new random weights."""
import json
from pathlib import Path
import sys
import torch
from torch import nn
from torch.utils.checkpoint import checkpoint
from campaign_artifacts import sha256


def support():
    intervals=[]
    for point in range(256,264):
        low,high=point-1,point+1  # tail3x3
        for _ in range(3):
            low-=8;high+=8       # four two-convolution residual blocks
            low//=2;high//=2     # nonoverlapping2x2 stride2 transpose
        low-=8;high+=8           # body
        for _ in range(3):
            low*=2;high=2*high+1 # backward through stride2 convolution
            low-=8;high+=8
        low-=1;high+=1           # head3x3
        intervals.append(dict(output=point,low=low,high=high))
    radius=max(max(r['output']-r['low'],r['high']-r['output']) for r in intervals)
    halo=(radius+7)//8*8
    return dict(packed_radius=radius,packed_halo=halo,cfa_halo=halo*2,
        packed_core=128,packed_context=128+2*halo,intervals=intervals,
        scope='Deepest-path maximum, eight stride phases; residual skip paths are shorter. Pinned fourblocks/stage,strideconv2,transposeconv2 topology.')


class DruCfaCorrector(nn.Module):
    """Predict standardized residual in canonical R/G1/G2/B sensor planes."""
    required_cfa_halo=384

    def __init__(self,core,checkpoint_stages=False):
        super().__init__();self.core=core;self.checkpoint_stages=checkpoint_stages

    def _residual(self,values):
        if not self.checkpoint_stages or not self.training or not torch.is_grad_enabled():return self.core(values)
        def stage(module,value):return checkpoint(module,value,use_reentrant=False,preserve_rng_state=False)
        x1=self.core.m_head(values)
        x2=stage(self.core.m_down1,x1);x3=stage(self.core.m_down2,x2);x4=stage(self.core.m_down3,x3)
        value=stage(self.core.m_body,x4)
        value=stage(self.core.m_up3,value+x4);value=stage(self.core.m_up2,value+x3);value=stage(self.core.m_up1,value+x2)
        return self.core.m_tail(value+x1)

    def correct_observations(self,packed,sigma,pattern):
        if (packed.ndim!=4 or packed.shape[1]!=4 or sigma.shape!=packed.shape or packed.dtype!=torch.float32
                or sigma.dtype!=packed.dtype or sigma.device!=packed.device or pattern.device!=packed.device
                or pattern.shape!=(len(packed),4) or pattern.dtype!=torch.int64
                or any(n%8 for n in packed.shape[-2:]) or min(packed.shape[-2:])<8
                or not torch.isfinite(packed).all() or not torch.isfinite(sigma).all() or (sigma<0).any()):
            raise ValueError('Finite float32 matching fourplane CFA/sigma,8cell grid and int64 Bayer labels required')
        expected=torch.tensor([[0,1,3,2]],device=packed.device).expand(len(packed),-1)
        if not torch.equal(pattern,expected):raise ValueError('Canonical R/G1/G2/B phase required')
        if (sigma==0).all():return packed
        residual=self._residual(torch.cat((packed,sigma),dim=1))
        if residual.shape!=packed.shape or not torch.isfinite(residual).all():raise ValueError('Invalid predicted sensor residual')
        corrected=torch.where(sigma==0,packed,packed-sigma*residual)
        if not torch.isfinite(corrected).all():raise ValueError('Nonfinite sensor output')
        return corrected


def create(root,seed):
    external=root/'build-patch_neural_restoration/third-party-drunet-v001'
    manifest=json.loads((external/'manifest.json').read_text(encoding='utf-8'))
    for item in manifest['files']:
        if sha256(external/item['path'])!=item['sha256']:raise ValueError('Pinned official DRUNet source changed')
    sys.path.insert(0,str(external.resolve()))
    from models.network_unet import UNetRes
    torch.manual_seed(seed)
    core=UNetRes(in_nc=8,out_nc=4,nc=[64,128,256,512],nb=4,act_mode='R',downsample_mode='strideconv',upsample_mode='convtranspose')
    if not isinstance(core.m_tail,nn.Conv2d):raise ValueError('Unexpected official tail')
    nn.init.zeros_(core.m_tail.weight)
    model=DruCfaCorrector(core,checkpoint_stages=True)
    return model,dict(repository=manifest['source_repository'],revision=manifest['source_commit'],
        source_manifest_sha256=sha256(external/'manifest.json'),pretrained_weights_loaded=False,
        seed=seed,parameters=sum(p.numel() for p in model.parameters()),support=support(),activation_checkpointing='nonreentrant perstage in training only, no stochastic layers')
