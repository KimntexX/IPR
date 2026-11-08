"""Low-dimensional scene-drift diagnostic, never a RAW/image correction.

One gain/offset per physical plane and one shared translation per frame pair.
The noise simulator is unchanged; raw and residual diagnostics are both kept.
"""
import numpy as np


def smooth(values):
    padded=np.pad(values,((0,0),(0,0),(2,2),(2,2)),mode='reflect')
    result=np.zeros_like(values,dtype=np.float64)
    for y in range(5):
        for x in range(5):result+=padded[...,y:y+values.shape[-2],x:x+values.shape[-1]]
    return result/25


def project(mean,difference,eligible):
    if (mean.ndim!=4 or mean.shape!=difference.shape or mean.shape[1]!=4 or min(mean.shape[-2:])<16
            or eligible.shape!=mean.shape[:2] or not eligible.any()
            or not np.isfinite(mean).all() or not np.isfinite(difference).all()):
        raise ValueError('Finite ROI,4,H,W pair and eligible field mask required')
    template=smooth(mean.astype(np.float64))
    gy,gx=np.gradient(template,axis=(-2,-1))
    columns=[];names=[]
    for plane in range(4):
        support=np.zeros_like(template);support[:,plane]=1
        columns.append(support);names.append(f'offset_plane_{plane}')
        center=float(template[:,plane].mean())
        columns.append(support*(template-center));names.append(f'gain_plane_{plane}')
    columns.extend((gx,gy));names.extend(('translation_x_packed','translation_y_packed'))
    design=np.stack(columns,axis=-1)
    fit_mask=np.broadcast_to(eligible[...,None,None],mean.shape).copy()
    fit_mask[...,:4,:]=False;fit_mask[...,-4:,:]=False
    fit_mask[...,:,:4]=False;fit_mask[...,:,-4:]=False
    a=design[fit_mask];b=difference[fit_mask]
    scales=np.linalg.norm(a,axis=0)
    present=scales>0
    normalized=a[:,present]/scales[present]
    coeff,_,rank,singular=np.linalg.lstsq(normalized,b,rcond=1e-10)
    full=np.zeros(len(columns),dtype=np.float64);full[present]=coeff/scales[present]
    fitted=np.einsum('...k,k->...',design,full,optimize=True)
    residual=difference-fitted
    return residual,dict(coefficients=dict(zip(names,map(float,full))),rank=int(rank),
        fit_pixels=int(fit_mask.sum()),dof_fraction=float(rank/fit_mask.sum()),
        condition=float(singular[0]/singular[-1]) if len(singular) and singular[-1]>0 else None,
        delta_mse=float(np.mean(difference**2)),residual_mse=float(np.mean(residual**2)),
        scope='Shared frame-pair nuisance diagnostic only; not sensor noise identification or target registration')
