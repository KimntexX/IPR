"""Native scalar/batch pixel parity, not merely metric agreement."""
import argparse
from pathlib import Path
import os
import numpy as np
from study_cfa_signal_transport import native
from transport_batch import native_batch


def main():
    p=argparse.ArgumentParser();p.add_argument('--tool',type=Path,required=True);args=p.parse_args()
    samples=[np.full((256,256),-.01),np.random.default_rng(11).normal(size=(256,256))*.001+.03]
    for source,(image,identity) in zip(samples,native_batch(args.tool,samples,os.environ.copy())):
        original,key=native(args.tool,source,os.environ.copy())
        if identity['output_sha256']!=key['outputs_sha256']['after_median']:raise ValueError('Pixel hash parity failed')
        np.testing.assert_array_equal(image,original['after_median'])
    print('Two native batch/scalar full pixel parities pass')


if __name__=='__main__':main()
