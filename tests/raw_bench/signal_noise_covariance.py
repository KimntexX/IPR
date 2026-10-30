"""Empirical second moments of independently simulated RGB residuals on five B3 levels."""
import cv2
import numpy as np


def transported_covariance(rgb):
    if rgb.shape!=(256,256,3) or not np.isfinite(rgb).all():
        raise ValueError('Invalid RGB residual')
    current=rgb.astype(float)@np.array([[.25,.5,.25],[.5,0,-.5],[-.5,1,-.5]]).T
    output=[]
    for level in range(5):
        step=1<<level
        kernel=np.zeros(4*step+1);kernel[::step]=np.array([1,4,6,4,1])/16
        base=cv2.sepFilter2D(current,-1,kernel,kernel,borderType=cv2.BORDER_REFLECT_101)
        detail=current-base
        output.append(np.cov(detail[64:192,64:192].reshape(-1,3),rowvar=False,ddof=1))
        current=base
    return np.array(output)
