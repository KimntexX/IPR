"""Re-decode through production RawDecoder and compare with controlled radiometry."""
import argparse
from datetime import datetime,timezone
import os
from pathlib import Path
import subprocess
import numpy as np
from analysis import native


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trials',required=True,type=Path)
    args=parser.parse_args()
    trials=native.read_json(args.trials);manifest=Path('config/raw-normalization.json')
    config=native.read_json(manifest);executable=Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe')
    now=datetime.now(timezone.utc);campaign='integration-radiometrie-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    folder=Path('build-colorimetrie-inventory')/campaign;folder.mkdir(parents=True,exist_ok=False)
    source_files=[args.trials,manifest,executable,Path(__file__),Path('src/raw/RawDecoder.cpp'),Path('src/raw/RawNormalization.cpp'),
                  Path('src/raw/RawNormalization.h'),Path('src/editor/imageeditor.cpp'),Path('src/qml/RawDevelopmentModule.qml')]
    hashes={str(p):native.sha256(p) for p in source_files};records=[]
    env=os.environ.copy();env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    for row in trials['records']:
        source=Path('tests/image/base')/(row['scene']+'.CR3')
        profile_hash=row['DCP_sha256']
        # Profile path is recorded in the controlled specification, not guessed by name.
        spec_item=next(x for x in trials['inputs'] if x['file'].endswith('specification.json'))
        spec=native.read_json(Path(spec_item['file']))
        profile=Path(next(x for x in spec['scenes'] if x['scene']==row['scene'])['DCP'])
        if native.sha256(source)!=row['RAW_sha256'] or native.sha256(profile)!=profile_hash:raise ValueError('Input changed')
        hashes[str(source)]=row['RAW_sha256'];hashes[str(profile)]=profile_hash
        out=folder/row['scene'];print('Production integration',row['scene'],flush=True)
        subprocess.run([str(executable.resolve()),str(source.resolve()),str(profile.resolve()),str(manifest.resolve()),str(out.resolve())],env=env,check=True,stdout=subprocess.DEVNULL)
        proof=native.read_json(out/'manifest.json')
        if not proof['normalization_applied']:
            raise ValueError('Production contract mismatch')
        pixels=np.fromfile(out/'encoded.f32',dtype='<f4').reshape(proof['rows'],proof['cols'],3)
        pixels=np.rint(pixels.astype(float)*65535)/65535
        mean=float((native.linearize(pixels) @ np.array([.2126,.7152,.0722])).mean())
        expected=row['models']['white_exposure_hue']['mean_Y'];error=abs(mean-expected)
        if error>1e-6:raise ValueError('Production differs from witness: '+row['scene']+' '+str(error))
        records.append(dict(scene=row['scene'],mean_Y=mean,witness_Y=expected,absolute_Y_error=error,
                            raw_sha256=entry['raw_sha256'],profile_sha256=profile_hash,white=proof['white_level'],
                            baseline_EV=proof['baseline_exposure_ev'],pixels_sha256=native.sha256(out/'encoded.f32')))
    for path,digest in hashes.items():
        if native.sha256(Path(path))!=digest:raise ValueError('Input changed during integration verification')
    result=dict(campaign_id=campaign,schema_version=1,data_version=1,group='base-colorimetrique',
                measured_at_utc=datetime.now(timezone.utc).isoformat(),inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                records=records,production_modified=True,validation='numerical integration only; visual highlight acceptance pending',
                method='Fresh full production decode, no sidecar/LCP, native grid step 8, 16-bit encoded output; exact audited manifest',
                units=dict(Y='relative linear sRGB luminance',error='absolute difference of mean Y, tolerance 1e-6'))
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
