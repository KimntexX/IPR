"""Bench data contracts, independent of RAW decoding and production rendering.

Single declared scalar scale shared by source, reference and sigma: no inferred
per-image range, clipping, WB change, gamma or colour conversion. Finite signed
camera values remain finite signed values. Masked losses require finite buffers.
"""
import math

import torch

from camera_unet import validate


def checked_scale(scale):
    if isinstance(scale, bool) or not isinstance(scale, (int, float)):
        raise ValueError('explicit numeric scale required')
    limits=torch.finfo(torch.float32)
    if not math.isfinite(scale) or not limits.tiny <= scale <= limits.max:
        raise ValueError('scale must be finite, positive and normal float32')
    return float(scale)


def normalize_pair(signal, target, sigma, scale=1.):
    validate(signal, sigma)
    if not isinstance(target, torch.Tensor) or target.shape != signal.shape:
        raise ValueError('reference extent must match input')
    if target.dtype != signal.dtype or target.device != signal.device:
        raise ValueError('reference type/device must match input')
    if not torch.isfinite(target).all():
        raise ValueError('nonfinite reference')
    scale = checked_scale(scale)
    normalized = tuple(value/scale for value in (signal,target,sigma))
    if not all(torch.isfinite(value).all() for value in normalized):
        raise ValueError('normalization overflow')
    for original,scaled in zip((signal,target,sigma),normalized):
        if ((original!=0)&(scaled==0)).any():
            raise ValueError('normalization underflow destroys a nonzero value')
    # Positive real scale may underflow when cast to tensor dtype. Validate sigma.
    validate(normalized[0],normalized[2])
    return normalized


def restore(signal, scale=1.):
    scale = checked_scale(scale)
    if not isinstance(signal, torch.Tensor) or signal.dtype != torch.float32:
        raise ValueError('float32 tensor required')
    if not torch.isfinite(signal).all():
        raise ValueError('nonfinite normalized result')
    result = signal*scale
    if not torch.isfinite(result).all():
        raise ValueError('restoration overflow')
    return result


def masked_mse(prediction, target, valid):
    if not isinstance(prediction, torch.Tensor) or not isinstance(target, torch.Tensor):
        raise ValueError('prediction/reference must be tensors')
    if prediction.ndim != 4 or prediction.shape[1] != 3 or prediction.shape != target.shape:
        raise ValueError('matching NCHW RGB expected')
    if prediction.dtype != torch.float32 or target.dtype != torch.float32:
        raise ValueError('float32 required')
    if prediction.device != target.device:
        raise ValueError('prediction/reference devices differ')
    if not torch.isfinite(prediction).all() or not torch.isfinite(target).all():
        raise ValueError('nonfinite loss input, including masked-out values')
    if not isinstance(valid,torch.Tensor) or valid.dtype != torch.bool:
        raise ValueError('explicit Boolean validity mask required')
    expected=(prediction.shape[0],1,*prediction.shape[2:])
    if valid.shape != expected or valid.device != prediction.device or not valid.any():
        raise ValueError('nonempty matching N1HW mask required')
    mask=valid.expand_as(prediction)
    # Index rather than average over all pixels: masked pixels do not dilute loss.
    loss=(prediction-target).square()[mask].mean()
    if not torch.isfinite(loss):
        raise ValueError('loss overflow')
    return loss


def validate_splits(records):
    """All variants/crops of one physical scene stay together; duplicates too.

    scene_group is supplied from capture provenance, never deduced from a crop,
    filename or ISO. Hash semantics (RAW or synthetic buffer) belong to manifest.
    """
    if not records:
        raise ValueError('empty partition')
    scene_roles,hash_roles,ids={}, {}, set()
    for row in records:
        if not isinstance(row,dict):
            raise ValueError('partition row must be an object')
        for key in ('id','scene_group','split','content_sha256'):
            if not isinstance(row.get(key),str) or not row[key].strip():
                raise ValueError('missing partition identity')
        role=row['split'];digest=row['content_sha256']
        if role not in ('train','validation','test'):
            raise ValueError('unknown partition role')
        hashes=[digest]
        if 'target_sha256' in row:
            hashes.append(row['target_sha256'])
        if any(not isinstance(h,str) or len(h)!=64 or
               any(c not in '0123456789abcdef' for c in h) for h in hashes):
            raise ValueError('invalid source/reference SHA-256')
        if row['id'] in ids:
            raise ValueError('duplicate example ID')
        ids.add(row['id'])
        for mapping,key in [(scene_roles,row['scene_group'])]+[(hash_roles,h) for h in hashes]:
            if key in mapping and mapping[key]!=role:
                raise ValueError('cross-split scene/content leakage')
            mapping[key]=role
    return {role:sum(row['split']==role for row in records)
            for role in ('train','validation','test')}
