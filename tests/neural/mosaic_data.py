"""Lossless signed Bayer packing: physical 2x2 positions, never guessed colour order."""
import numpy as np


def pack_mosaic(image):
    if (image.ndim!=2 or image.dtype!=np.float32 or min(image.shape)<2
            or any(v%2 for v in image.shape) or not np.isfinite(image).all()):
        raise ValueError('Finite even-sized float32 mosaic required')
    return np.stack([image[y::2,x::2] for y,x in ((0,0),(0,1),(1,0),(1,1))])


def unpack_mosaic(planes):
    if (planes.ndim!=3 or planes.shape[0]!=4 or planes.dtype!=np.float32
            or min(planes.shape[1:])<1 or not np.isfinite(planes).all()):
        raise ValueError('Finite 4HW float32 planes required')
    result=np.empty((2*planes.shape[1],2*planes.shape[2]),np.float32)
    for i,(y,x) in enumerate(((0,0),(0,1),(1,0),(1,1))):result[y::2,x::2]=planes[i]
    return result


def pattern_gains(pattern,gains):
    pattern=np.asarray(pattern)
    gains=np.asarray(gains,dtype=np.float64)
    if (pattern.shape!=(2,2) or pattern.dtype.kind not in 'iu' or not np.isin(pattern,[0,1,2,3]).all()
            or np.count_nonzero(pattern==0)!=1 or np.count_nonzero(pattern==2)!=1
            or gains.shape!=(4,) or not np.isfinite(gains).all() or np.any(gains<=0)):
        raise ValueError('Bayer R/G/B/G2 pattern and positive four gains required')
    return gains[pattern.reshape(4)].astype(np.float32)


def read_stage(capture,stage):
    from pathlib import Path
    path=Path(capture['descriptor']).parent/capture['pixels']
    if path.resolve().parent!=Path(capture['descriptor']).resolve().parent:
        raise ValueError('Invalid mosaic pixel path')
    if stage['dtype']!='<f4' or stage['shape']!=[384,384,1] or stage['offset_bytes']<0:
        raise ValueError('Invalid mosaic stage contract')
    with path.open('rb') as handle:
        handle.seek(stage['offset_bytes']);data=handle.read(384*384*4)
    if len(data)!=384*384*4:raise ValueError('Truncated mosaic')
    return np.frombuffer(data,dtype='<f4').reshape(384,384).copy()
