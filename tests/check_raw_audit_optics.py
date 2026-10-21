"""Match current saved optical recipes to manual RAW_Audit exports, read-only sources."""
import argparse
from datetime import datetime,timezone
import os
from pathlib import Path
import subprocess
import numpy as np
from analysis import native
from inspect_saved_raw_state import inspect


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('exports',type=Path);args=parser.parse_args()
    memory=native.read_json(args.exports);now=datetime.now(timezone.utc)
    campaign='audit-export-optique-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    folder=Path('build-colorimetrie-inventory')/campaign;folder.mkdir(parents=True,exist_ok=False)
    executable=Path('build-windows-msvc/Release/rawNormalizationCorpusProbe.exe');manifest=Path('config/raw-normalization.json')
    inputs=[args.exports,executable,manifest,Path(__file__),Path('tests/inspect_saved_raw_state.py'),Path('tests/raw_normalization_corpus_probe.cpp')]
    hashes={str(p):native.sha256(p) for p in inputs};records=[]
    env=os.environ.copy();env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    for row in memory['records']:
        raw=Path('tests/image/base')/(row['scene']+'.CR3');sidecar=Path(str(raw)+'.ipr');state=inspect(sidecar)
        recipe=state['raw'];hashes[str(sidecar)]=state['sha256'];hashes[str(raw)]=native.sha256(raw)
        result=dict(scene=row['scene'],saved_state=state,export_metadata=row['IPR_metadata'],
                    export_mean_Y=row['IPR']['mean_Y'],LR_mean_Y=row['LR']['mean_Y'])
        if recipe['normalizationEnabled'] and recipe['lensCorrectionEnabled'] and recipe['chromaticAberrationEnabled']:
            print('Reproducing optical recipe',row['scene'],flush=True)
            profile,lcp=Path(recipe['profile']),Path(recipe['lensProfile'])
            if native.sha256(profile)!=recipe['profileSha256'] or native.sha256(lcp)!=recipe['lensProfileSha256']:
                raise ValueError('Saved profile no longer matches')
            hashes[str(profile)]=recipe['profileSha256'];hashes[str(lcp)]=recipe['lensProfileSha256']
            out=folder/row['scene']
            subprocess.run([str(executable.resolve()),str(raw.resolve()),str(profile.resolve()),str(manifest.resolve()),str(out.resolve()),str(lcp.resolve())],env=env,check=True,stdout=subprocess.DEVNULL)
            proof=native.read_json(out/'manifest.json')
            pixels=np.fromfile(out/'encoded.f32',dtype='<f4').reshape(proof['rows'],proof['cols'],3)
            encoded=np.rint(pixels.astype(float)*65535)/65535
            Y=float((native.linearize(encoded) @ np.array([.2126,.7152,.0722])).mean())
            result.update(reproduced_optics=proof,reproduced_mean_Y=Y,
                          export_to_reproduced_relative_percent=100*(row['IPR']['mean_Y']/Y-1),
                          output_pixels_sha256=native.sha256(out/'encoded.f32'))
        else:result['reproduction_status']='No optical replay: saved flags not both enabled'
        records.append(result)
    for path,sha in hashes.items():
        if native.sha256(Path(path))!=sha:raise ValueError('Input changed during optical replay')
    result=dict(campaign_id=campaign,schema_version=1,data_version=1,group='base-colorimetrique',
                measured_at_utc=datetime.now(timezone.utc).isoformat(),records=records,
                inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                user_reported={'7599_IPR_lens_and_CA_enabled':True,'earlier_all_disabled_answer_referred_to':'Lightroom',
                               '0081_orange_improved':True,'0043_previous_vibrance_export_replaced':True},
                method='Fresh production decode with saved DCP/LCP, calibrated RAW, camera WB, no photo adjustments; native step 8',
                units={'Y':'relative linear sRGB luminance','difference':'100*(manual export/replay-1)'},
                limitations=['Saved sidecar is current state, not independently timestamped export recipe',
                             'Differences from no-optics LR are not attributed to calibration alone',
                             'Scalar agreement is not proof of pixel identity, detail preservation or physical fidelity'])
    target=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(target,result);print(target)


if __name__=='__main__':main()
