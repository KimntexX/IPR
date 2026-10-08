#!/usr/bin/env python3
"""Compare full production decodes to the independent normalized Neutral witness."""
import argparse
import json
from pathlib import Path
import numpy as np
from analyse_base_colorimetrique_v2 import file_hash
from analyse_canon_white_levels import load_stage
from investigate_dcp_curve import PROPHOTO_TO_SRGB, encoded, hue_stable, curve
from investigate_dcp_normalization import select_unique
from investigate_dcp_order import render
from investigate_tonal_base import read_dcp, local_path


def guarded_curve(rgb, points):
    bounded=np.clip(rgb,0,1)
    low,high=bounded.min(axis=-1),bounded.max(axis=-1)
    reversed_extrema=curve(high,points)<curve(low,points)
    stable=hue_stable(bounded,points)
    return np.where(reversed_extrema[...,None],curve(bounded,points),stable),reversed_extrema


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('probe','measure','profile','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('production',nargs='+',type=Path)
    args=parser.parse_args()
    if args.output.exists(): parser.error('Refuse to overwrite evidence')
    manifest=json.loads((args.probe/'manifest.json').read_text(encoding='utf-8'))
    measure=json.loads(args.measure.read_text(encoding='utf-8'))
    tags=read_dcp(args.profile)
    if file_hash(args.profile) not in measure['inputs_sha256'].values():
        raise ValueError('Profile differs from controlled evidence')
    result={'schema':1,'analyser_sha256':file_hash(Path(__file__)),
            'measurement_sha256':file_hash(args.measure),'scenes':[]}
    for folder in args.production:
        proof=json.loads((folder/'manifest.json').read_text(encoding='utf-8'))
        scene=Path(proof['raw']).stem
        row=next(x for x in measure['scenes'] if x['scene']==scene)
        fixed=select_unique(manifest,scene,'specular_fixed')
        if (file_hash(local_path(fixed['raw']))!=row['raw_sha256'] or
                not proof['normalization_applied'] or not proof['no_lcp'] or
                proof['white_level']!=row['dng_white'] or
                proof['baseline_exposure_ev']!=row['dng_baseline_exposure_ev']):
            raise ValueError('Production normalization contract mismatch')
        camera=load_stage(args.probe,fixed,'camera_rgb')
        pre,_=render(camera*2**row['dng_baseline_exposure_ev'],fixed['wb_kelvin'],tags)
        pre=pre @ np.linalg.inv(PROPHOTO_TO_SRGB).T
        ideal=encoded(hue_stable(pre,tags[50940]) @ PROPHOTO_TO_SRGB.T)
        guarded,reversed_extrema=guarded_curve(pre,tags[50940])
        expected=encoded(guarded @ PROPHOTO_TO_SRGB.T)
        actual=np.fromfile(folder/'encoded.f32',dtype='<f4').reshape(proof['rows'],proof['cols'],3)
        actual=np.rint(actual.astype(float)*65535)/65535
        if actual.shape!=expected.shape: raise ValueError('Production geometry mismatch')
        errors=np.abs(actual-expected)*65535
        values={'scene':scene,'samples':int(actual.shape[0]*actual.shape[1]),
                'max_code_error':float(errors.max()),'mean_code_error':float(errors.mean()),
                'guarded_extrema_fraction':float(reversed_extrema.mean()),
                'guard_vs_original_experiment_max_code_error':float(np.abs(expected-ideal).max()*65535),
                'production_vs_original_experiment_max_code_error':float(np.abs(actual-ideal).max()*65535),
                'raw_sha256':row['raw_sha256'],
                'production_pixels_sha256':file_hash(folder/'encoded.f32'),
                'production_manifest_sha256':file_hash(folder/'manifest.json')}
        if errors.max()>4.000001 or errors.mean()>.03:
            raise ValueError('Production differs from independent witness: '+str(values))
        result['scenes'].append(values)
        print(values,flush=True)
    result['production_source_sha256']={str(p):file_hash(p) for p in
        map(Path,('src/raw/RawNormalization.cpp','src/raw/RawDecoder.cpp','src/profiles/CameraProfileEngine.cpp'))}
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2,allow_nan=False)
        stream.write('\n')


if __name__=='__main__': main()
