"""Signed-camera transfer metrics; neither perceptual colour nor clean truth."""
import torch
from torch.nn import functional as F


def green_gradients(image,smooth=False):
    green=image[:,1:2]
    if smooth:
        kernel=torch.tensor([1.,4.,6.,4.,1.],device=image.device)/16
        kernel=(kernel[:,None]*kernel[None,:])[None,None]
        green=F.conv2d(F.pad(green,(2,2,2,2),mode='reflect'),kernel)
    return torch.cat(((green[:,:,1:-1,2:]-green[:,:,1:-1,:-2])*.5,
                      (green[:,:,2:,1:-1]-green[:,:,:-2,1:-1])*.5),dim=1)


def core_mask(valid,border=24):
    if valid.ndim!=4 or valid.shape[1]!=1 or valid.dtype!=torch.bool:
        raise ValueError('N1HW Boolean mask required')
    if border<0 or min(valid.shape[-2:])<=2*border:
        raise ValueError('Insufficient context-free interior')
    result=valid.clone()
    if border:
        result[:,:,:border]=False;result[:,:,-border:]=False
        result[:,:,:,:border]=False;result[:,:,:,-border:]=False
    return result


def structure_thresholds(training_target,training_valid):
    mask=core_mask(training_valid)[:,:,1:-1,1:-1]
    amplitude=green_gradients(training_target,True).square().sum(dim=1,keepdim=True).sqrt()
    if not mask.any():raise ValueError('No training support for structure thresholds')
    quantiles=torch.quantile(amplitude[mask],torch.tensor([.25,.75]))
    return [float(v) for v in quantiles]


def error_sums(source,target,output,valid):
    count=int(valid.sum())
    if not count:return dict(pixels=0)
    mask=valid.expand_as(source)
    before=source-target;after=output-target
    channel_bias=[float(after[:,c][valid[:,0]].sum()) for c in range(3)]
    channels=[]
    for delta in (before,after):
        channels.append([float(delta[:,c].square()[valid[:,0]].sum()) for c in range(3)])
    opponent=[]
    for delta in (before,after):
        value=torch.stack((delta[:,0]-delta[:,1],delta[:,2]-delta[:,1]),dim=1)
        opponent.append(float(value.square()[valid.expand_as(value)].sum()))
    return dict(pixels=count,input_sse=float(before.square()[mask].sum()),
                output_sse=float(after.square()[mask].sum()),channel_bias_sum=channel_bias,
                input_channel_sse=channels[0],output_channel_sse=channels[1],
                input_opponent_sse=opponent[0],output_opponent_sse=opponent[1])


def gradient_sums(source,target,output,valid,thresholds):
    reference=green_gradients(target)
    before=green_gradients(source);after=green_gradients(output)
    strength=green_gradients(target,True).square().sum(dim=1,keepdim=True).sqrt()
    context=(F.conv2d(valid.float(),torch.ones((1,1,5,5)),padding=2)>=25.)
    support=(core_mask(valid)&context)[:,:,1:-1,1:-1]
    low,high=thresholds
    if not 0<=low<=high:raise ValueError('Ordered nonnegative structure thresholds required')
    bands=dict(low=strength<=low,weak=(strength>low)&(strength<high),
               strong=(strength>low)&(strength>=high))
    result={}
    for name,band in bands.items():
        mask=(support&band).expand_as(reference)
        result[name]=dict(components=int(mask.sum()),reference_energy=float(reference.square()[mask].sum()),
            input_projection=float((before*reference)[mask].sum()),
            output_projection=float((after*reference)[mask].sum()),
            input_gradient_sse=float((before-reference).square()[mask].sum()),
            output_gradient_sse=float((after-reference).square()[mask].sum()))
    return result


def summarize(rows):
    result={}
    for region in ('full','core24'):
        scored=[r[region] for r in rows if r[region]['pixels']>0]
        count=sum(r['pixels'] for r in scored)
        if not count:
            result[region]=dict(pixels=0,status='no_valid_support');continue
        before=sum(r['input_sse'] for r in scored)
        after=sum(r['output_sse'] for r in scored)
        opp_before=sum(r['input_opponent_sse'] for r in scored)
        opp_after=sum(r['output_opponent_sse'] for r in scored)
        result[region]=dict(pixels=count,input_mse=before/(3*count),output_mse=after/(3*count),
            mse_ratio=after/before if before else None,
            opponent_mse_ratio=opp_after/opp_before if opp_before else None,
            camera_channel_bias=[sum(r['channel_bias_sum'][c] for r in scored)/count for c in range(3)])
    result['structure']={}
    for band in ('low','weak','strong'):
        sums={key:sum(r['structure'][band][key] for r in rows) for key in
              ('components','reference_energy','input_projection','output_projection',
               'input_gradient_sse','output_gradient_sse')}
        energy=sums['reference_energy'];baseline=sums['input_gradient_sse']
        result['structure'][band]=dict(**sums,input_gradient_gain=sums['input_projection']/energy if energy else None,
            output_gradient_gain=sums['output_projection']/energy if energy else None,
            gradient_error_ratio=sums['output_gradient_sse']/baseline if baseline else None)
    return result
