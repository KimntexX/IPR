"""Numerical photo-pair diagnostics, independent of file access and training.

No noisy source is resampled. Only the burst reference is interpolated into
source coordinates. Quality gates do not use any learned-model output.
"""
import cv2
import numpy as np


def finite_rgb(image):
    if image.ndim!=3 or image.shape[2]!=3 or image.dtype!=np.float32 or not np.isfinite(image).all():
        raise ValueError('Finite HWC float32 RGB required')


def balanced_gains(neutral):
    neutral=np.asarray(neutral,dtype=np.float64)
    if neutral.shape!=(3,) or not np.isfinite(neutral).all() or np.any(neutral<=0):
        raise ValueError('Positive camera-neutral vector required')
    return (neutral.max()/neutral).astype(np.float32)


def sigma_proxy(image):
    """Single-image Haar MAD proxy, affected by texture and demosaic correlation."""
    finite_rgb(image)
    h,w=image.shape[:2];h-=h%2;w-=w%2
    if min(h,w)<4:
        raise ValueError('Insufficient sigma extent')
    hh=(image[:h:2,:w:2]-image[1:h:2,:w:2]-image[:h:2,1:w:2]+image[1:h:2,1:w:2])*.5
    centered=hh-np.median(hh,axis=(0,1))
    return np.maximum(np.median(np.abs(centered),axis=(0,1))/.67448975,1e-6).astype(np.float32)


def blur(image,sigma=4.):
    return cv2.GaussianBlur(image,(0,0),sigma,borderType=cv2.BORDER_REFLECT101)


def translation(reference,source,scale_xy,ecc_floor=.97,camera_limit=.9,
                minimum_overlap_fraction=.5,reciprocal_max_error_pixels=1.):
    if reference.shape!=source.shape or reference.ndim!=2 or min(reference.shape)<32 or not np.isfinite(reference).all() or not np.isfinite(source).all():
        raise ValueError('Matching finite green proxies required')
    scale=np.asarray(scale_xy,dtype=np.float64)
    if (scale.shape!=(2,) or not np.isfinite(scale).all() or np.any(scale<=0)
            or not 0<minimum_overlap_fraction<=1 or reciprocal_max_error_pixels<=0
            or not np.isfinite(reciprocal_max_error_pixels)):
        raise ValueError('Valid proxy scale, overlap and reciprocity bounds required')
    arrays=[]
    for image in (reference,source):
        # Identical crops preserve translation coordinates and exclude border
        # artifacts from both the camera extent and synthetic shift fixtures.
        value=blur(image.astype(np.float32),1.5)[8:-8,8:-8]
        deviation=float(value.std())
        if deviation<1e-6:
            return dict(accepted=False,reason='unobservable_flat_registration')
        arrays.append((value-float(value.mean()))/deviation)
    # RawDevelopment clips the balanced CFA at camera white 1. Exclude
    # clipped/nonlinear highlights, also after multiplying the exposure.
    common=((reference<camera_limit)&(source<camera_limit)).astype(np.uint8)
    common=cv2.erode(common,np.ones((9,9),np.uint8))[8:-8,8:-8]
    if np.count_nonzero(common)<256:
        return dict(accepted=False,reason='insufficient_unsaturated_registration_support')
    matrix=np.eye(2,3,dtype=np.float32)
    try:
        score,matrix=cv2.findTransformECC(*arrays,matrix,cv2.MOTION_TRANSLATION,
            (cv2.TERM_CRITERIA_COUNT|cv2.TERM_CRITERIA_EPS,80,1e-6),common,5)
        reverse_score,reverse=cv2.findTransformECC(*arrays[::-1],np.eye(2,3,dtype=np.float32),
            cv2.MOTION_TRANSLATION,
            (cv2.TERM_CRITERIA_COUNT|cv2.TERM_CRITERIA_EPS,80,1e-6),common,5)
    except cv2.error:
        return dict(accepted=False,reason='registration_failed')
    shift=matrix[:,2]*scale
    reverse_shift=reverse[:,2]*scale
    extent=np.array([source.shape[1],source.shape[0]],dtype=np.float64)
    overlap=float(np.prod(np.maximum(0.,extent-np.abs(matrix[:,2])))/np.prod(extent))
    error=float(np.max(np.abs(shift+reverse_shift)))
    finite=bool(np.isfinite(shift).all() and np.isfinite(reverse_shift).all()
                and np.isfinite([score,reverse_score,error,overlap]).all())
    accepted=bool(finite and min(score,reverse_score)>=ecc_floor
                  and overlap>=minimum_overlap_fraction and error<=reciprocal_max_error_pixels)
    reason=('measured_reciprocal_translation' if accepted else
            'registration_correlation_overlap_or_reciprocity_failed')
    return dict(accepted=accepted,ecc=float(score),reverse_ecc=float(reverse_score),
                shift_xy=shift.tolist(),reverse_shift_xy=reverse_shift.tolist(),
                full_frame_overlap_fraction=overlap,reciprocal_error_pixels=error,reason=reason)


def warp(image,shift_xy,to_reference=False,mask=False):
    shift=np.asarray(shift_xy,dtype=np.float32)
    if shift.shape!=(2,) or not np.isfinite(shift).all():
        raise ValueError('Finite translation required')
    matrix=np.eye(2,3,dtype=np.float32);matrix[:,2]=shift
    flags=cv2.INTER_LINEAR
    if to_reference: flags|=cv2.WARP_INVERSE_MAP
    result=cv2.warpAffine(image.astype(np.float32) if mask else image,matrix,
        (image.shape[1],image.shape[0]),flags=flags,borderMode=cv2.BORDER_CONSTANT,borderValue=0)
    # Every contributing bilinear neighbor must be valid, not just the nearest.
    return (result>=1.-1e-7).astype(np.uint8) if mask else result


def reference_tile(frames,shifts,gains,protocol):
    if len(frames)<8 or len(frames)!=len(shifts) or len(frames)!=len(gains):
        raise ValueError('At least eight aligned reference captures required')
    aligned=[];usable=[]
    for image,shift,gain in zip(frames,shifts,gains):
        finite_rgb(image)
        aligned.append(warp(image,shift,to_reference=True))
        mask=np.all(image<protocol['unsaturated_camera_fraction'],axis=2)
        usable.append(warp(mask,shift,to_reference=True,mask=True)>0)
    stack=np.stack(aligned)
    mean=stack.mean(axis=0,dtype=np.float64).astype(np.float32)
    # Normalize each frame only for the stability diagnostic, not for averaging.
    low=np.stack([blur(a/g,protocol['reference_lowpass_sigma']) for a,g in zip(aligned,gains)])
    deviation=low.std(axis=0,ddof=1)
    stable=np.max(deviation,axis=2)<protocol['reference_stability_dn_fraction']
    half=stack[::2].mean(axis=0)-stack[1::2].mean(axis=0)
    valid=stable&np.all(usable,axis=0)
    valid[:16]=False;valid[-16:]=False;valid[:,:16]=False;valid[:,-16:]=False
    return mean,valid,dict(reference_count=len(frames),stable_fraction=float(stable[16:-16,16:-16].mean()),
        valid_fraction=float(valid[16:-16,16:-16].mean()),
        lowpass_temporal_std_median=float(np.median(deviation[16:-16,16:-16])),
        half_mean_rmse=float(np.sqrt(np.mean(half[16:-16,16:-16]**2))),
        negative_reference_fraction=float((mean<0).mean()))


def affine_fit(reference_tiles,noisy_tiles,masks,fit_rois):
    """One robust gain/offset per channel over frozen fit ROIs, not per tile."""
    xs=[];ys=[]
    for i in fit_rois:
        reference=reference_tiles[i];noisy=noisy_tiles[i]
        finite_rgb(reference);finite_rgb(noisy)
        mask=masks[i][::6,::6]
        xs.append(blur(reference)[::6,::6][mask]);ys.append(blur(noisy)[::6,::6][mask])
    x=np.concatenate(xs).astype(np.float64);y=np.concatenate(ys).astype(np.float64)
    if len(x)<256:
        raise ValueError('Insufficient independent radiometric fit support')
    gains=[];offsets=[]
    for c in range(3):
        if np.ptp(x[:,c])<.01:
            raise ValueError('Radiometric fit cannot identify gain')
        design=np.column_stack((x[:,c],np.ones(len(x))))
        weights=np.ones(len(x))
        for _ in range(4):
            coefficients=np.linalg.lstsq(design*weights[:,None],y[:,c]*weights,rcond=None)[0]
            residual=y[:,c]-design@coefficients
            mad=np.median(np.abs(residual-np.median(residual)))/.67448975
            weights=np.sqrt(np.minimum(1.,4*max(float(mad),1e-6)/np.maximum(np.abs(residual),1e-12)))
        gains.append(float(coefficients[0]));offsets.append(float(coefficients[1]))
    return np.array(gains,dtype=np.float32),np.array(offsets,dtype=np.float32)


def tile_quality(noisy,target,valid,gain,sigma,protocol):
    finite_rgb(noisy);finite_rgb(target)
    if valid.shape!=noisy.shape[:2] or valid.dtype!=bool:
        raise ValueError('Matching Boolean validity mask required')
    valid=valid&np.all(noisy<protocol['unsaturated_camera_fraction'],axis=2)
    valid[:16]=False;valid[-16:]=False;valid[:,:16]=False;valid[:,-16:]=False
    count=int(valid.sum())
    fraction=count/float((valid.shape[0]-32)*(valid.shape[1]-32))
    if not count:
        return valid,dict(accepted=False,reason='no_valid_reference_pixels',valid_fraction=0.)
    residual=blur(noisy-target,protocol['reference_lowpass_sigma'])
    rmse=np.sqrt(np.mean(residual[valid]**2,axis=0))
    # This is a provisional pair-quality threshold, not a sensor noise model.
    limit=protocol['tile_radiometry_floor']*gain+protocol['tile_radiometry_noise_multiplier']*sigma/(2*np.sqrt(np.pi)*protocol['reference_lowpass_sigma'])
    accepted=fraction>=protocol['minimum_reference_valid_fraction'] and np.all(rmse<=limit)
    return valid,dict(accepted=bool(accepted),reason='low_frequency_pair_consistency',
        valid_fraction=fraction,lowpass_rmse=rmse.tolist(),lowpass_limit=limit.tolist())


def patches(noisy,target,valid,sigma,protocol):
    side=96
    for y in protocol['patch_origins']:
        for x in protocol['patch_origins']:
            mask=valid[y:y+side,x:x+side]
            if mask.shape!=(side,side) or mask.mean()<protocol['minimum_patch_valid_fraction']:
                continue
            yield dict(origin_xy=[x,y],signal=noisy[y:y+side,x:x+side].transpose(2,0,1).copy(),
                target=target[y:y+side,x:x+side].transpose(2,0,1).copy(),
                valid=mask[None].copy(),sigma=sigma.copy())
