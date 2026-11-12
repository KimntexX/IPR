"""Learned two-level CFA context, preserving spatial/colour transpose symmetry."""
import torch
from torch import nn
from torch.nn import functional as F

from closed_moment_unet import equivariant_block, paired_permutation, TransposeConv
from cfa_correlation import correlation_features, WINDOWS, PACKED_RADIUS


def unet_bounds(index):
    """Packed-input support of two-conv blocks, two pools and nearest skips."""
    low_middle = (((index-2)//2)-2)//2
    high_middle = (((index+2)//2)+2)//2
    return 4*low_middle-14, 4*high_middle+17


def composed_support():
    """All pooling phases; correlation enters after the first two convolutions."""
    records = []
    for cell in range(4):
        low, high = unet_bounds(cell)
        # The interpolation coefficient sees corrected cells [low,high].
        left = unet_bounds(low)[0]+2-PACKED_RADIUS
        right = unet_bounds(high)[1]-2+PACKED_RADIUS
        # Other routes (local descriptors and interpolation taps) are shorter.
        records.append(dict(pool_phase=cell, minimum_packed_offset=left-cell,
            maximum_packed_offset=right-cell))
    maximum = max(max(-r['minimum_packed_offset'], r['maximum_packed_offset']) for r in records)
    # Include either subpixel of the output CFA cell, then align to8 pixels.
    halo = ((2*maximum+1+7)//8)*8
    return dict(phases=records, maximum_packed_radius=maximum,
        conservative_full_pixel_halo=halo, training_side=256, scoring_border=112, scoring_side=32,
        engineering_side=288, engineering_core=64)


class CfaContextFeatures(nn.Module):
    """Own trainable context; reconstruction parameters are not shared."""
    def __init__(self, inputs, width):
        super().__init__()
        p1, p2, p4 = (paired_permutation(n*width) for n in (1, 2, 4))
        concatenate = lambda a, b: a+tuple(len(a)+v for v in b)
        self.enc1 = equivariant_block(inputs, p1)
        self.enc2 = equivariant_block(p1, p2)
        self.middle = equivariant_block(p2, p4)
        self.dec2 = equivariant_block(concatenate(p4, p2), p2)
        self.dec1 = equivariant_block(concatenate(p2, p1), p1)
        physical, axes = (0, 2, 1, 3), (2, 3, 0, 1)
        permutation = tuple(s*16+physical[p]*4+axes[c] for s in range(len(WINDOWS)) for p in range(4) for c in range(4))
        self.statistics = TransposeConv(permutation, p1, 1)
        nn.init.zeros_(self.statistics.weight)
        nn.init.zeros_(self.statistics.bias)

    def forward(self, features):
        a = self.enc1(features)
        statistics = torch.cat([correlation_features(features[:, :4], features[:, 4:8], w).flatten(1, 2)
            for w in WINDOWS], dim=1).asinh()
        a = a+self.statistics(statistics)
        b = self.enc2(F.avg_pool2d(a, 2))
        c = self.middle(F.avg_pool2d(b, 2))
        d = self.dec2(torch.cat((F.interpolate(c, size=b.shape[2:], mode='nearest'), b), dim=1))
        return self.dec1(torch.cat((F.interpolate(d, size=a.shape[2:], mode='nearest'), a), dim=1))
