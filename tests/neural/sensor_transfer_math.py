"""Least-squares periodic response at actual CFA sample sites, diagnostic only."""
import torch


def site_response(values,truth,wave,quadrature,border=32):
    if values.shape!=truth.shape or values.ndim!=4 or values.shape[:2]!=(1,4) or not torch.isfinite(values).all():
        raise ValueError('Matching finite packed Bayer planes required')
    rows=[]
    for plane in range(4):
        y,x=divmod(plane,2)
        w=wave[y::2,x::2][border:-border,border:-border].double().reshape(-1)
        q=quadrature[y::2,x::2][border:-border,border:-border].double().reshape(-1)
        actual=values[0,plane,border:-border,border:-border].double().reshape(-1)
        clean=truth[0,plane,border:-border,border:-border].double().reshape(-1)
        if len(w)!=len(actual) or len(w)<16:raise ValueError('Physical CFA core differs')
        design=torch.stack((torch.ones_like(w),w,q),dim=1);inverse=torch.linalg.pinv(design)
        measured=inverse@actual;ideal=inverse@clean
        if abs(float(ideal[1]))<1e-8:raise ValueError('Degenerate clean periodic reference')
        rows.append(dict(sine_gain=float(measured[1]/ideal[1]),quadrature_gain=float(measured[2]/ideal[1]),
            dc_error=float(measured[0]-ideal[0]),mse=float((actual-clean).square().mean())))
    return rows
