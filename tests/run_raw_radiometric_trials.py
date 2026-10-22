"""Fresh per-RAW white-level/baseline ablations, never modifies production config."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import statistics
import subprocess

from analysis import native


def aggregate(records):
    cohorts={'all':records}
    for camera in sorted({row['camera'] for row in records}):
        cohorts[camera]=[r for r in records if r['camera']==camera]
    result={}
    for cohort,rows in cohorts.items():
        models={}
        for name in sorted(rows[0]['models']):
            values=[r['models'][name]['relative_mean_Y_to_LR_percent'] for r in rows]
            deltas=[r['models'][name]['encoded_clipped_percent']-r['models']['channel']['encoded_clipped_percent'] for r in rows]
            models[name]=dict(scenes=len(rows),mean_signed_Y_error_percent=statistics.mean(values),
                             mean_absolute_Y_error_percent=statistics.mean(map(abs,values)),
                             worse_absolute_Y_than_current=sum(abs(v)>abs(r['models']['channel']['relative_mean_Y_to_LR_percent']) for r,v in zip(rows,values)),
                             max_added_clipping_percentage_points=max(deltas))
        result[cohort]=models
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit',required=True,type=Path)
    parser.add_argument('--summary',required=True,type=Path)
    parser.add_argument('--probe',type=Path,default=Path('build-windows-msvc/Release/tonalBaseProbe.exe'))
    args=parser.parse_args()
    audit=native.read_json(args.audit);summary=native.read_json(args.summary)
    rows=audit['records'];references={r['scene']:r for r in summary['comparisons']}
    if summary['missing_LR'] or len(rows)!=len(references) or {r['scene'] for r in rows}!=set(references):
        raise ValueError('Incomplete reference corpus')
    now=datetime.now(timezone.utc)
    campaign='essais-radiometriques-ouverture-v001-'+now.strftime('%Y%m%dT%H%M%S%fZ')
    folder=Path('build-colorimetrie-inventory')/campaign;folder.mkdir(parents=True,exist_ok=False)
    inputs=[args.audit,args.summary,args.probe,Path(__file__),Path('tests/tonal_base_probe.cpp'),
            Path('tests/raw_opening_trial_helpers.h'),Path('src/raw/RawDecoder.cpp'),Path('src/raw/RawDecoder.h'),
            Path('src/raw/RawBlackLevelEstimator.cpp'),Path('src/raw/RawBlackLevelEstimator.h'),
            Path('src/profiles/CameraProfileEngine.cpp'),Path('src/profiles/CameraProfileEngine.h'),
            Path('src/core/ColorPipeline.cpp'),Path('src/core/ColorPipeline.h')]
    # Audit data remains historical; production source changes would require reassessment.
    for item in audit['inputs']:
        if item['file'].startswith('src/') and native.sha256(Path(item['file']))!=item['sha256']:
            raise ValueError('Production source changed since calibration audit')
    scenes=[]
    for row in rows:
        reference=references[row['scene']];profile=row['deployed_CameraNeutral']
        for path,expected in ((Path(row['raw_path']),row['raw_sha256']),
                              (Path(profile['path']),profile['sha256']),
                              (Path(reference['LR_file']),reference['LR_sha256'])):
            if native.sha256(path)!=expected:raise ValueError('Input no longer matches audited evidence: '+str(path))
            inputs.append(path)
        scenes.append(dict(scene=row['scene'],RAW=str(Path(row['raw_path']).resolve()),RAW_sha256=row['raw_sha256'],
                           DCP=profile['path'],DCP_sha256=profile['sha256'],camera=row['LibRaw']['camera'],
                           white_level=row['comparison']['dng_white'],camera_baseline_EV=row['comparison']['camera_baseline_EV']))
    spec=folder/'specification.json';native.write_immutable(spec,dict(scenes=scenes));inputs.append(spec)
    hashes={str(p):native.sha256(p) for p in inputs}
    env=os.environ.copy()
    if os.name=='nt':env['PATH']=';'.join(['C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin','C:/dev/vcpkg/installed/x64-windows/bin',env.get('PATH','')])
    output=folder/'native'
    subprocess.run([str(args.probe.resolve()),'--radiometric',str(spec.resolve()),str(output.resolve())],env=env,check=True,timeout=1200)
    result=native.read_json(output/'result.json')
    if len(result['records'])!=len(rows) or {r['scene'] for r in result['records']}!=set(references):
        raise ValueError('Incomplete native experiment')
    for row in result['records']:
        reference=references[row['scene']];expected=reference['models']['current']['mean_Y']
        if abs(row['models']['channel']['mean_Y']-expected)>1e-7:
            raise ValueError('Current rendering witness diverged: '+row['scene'])
        row['current_Y_witness_absolute_error']=abs(row['models']['channel']['mean_Y']-expected)
        a=row['decodes']['current_white']['after_process'];b=row['decodes']['fixed_white']['after_process']
        if a['pre_mul']!=b['pre_mul']:raise ValueError('WB changed between white-level variants')
        if row['decodes']['current_white']['after_black_estimation']!=row['decodes']['fixed_white']['after_black_estimation']:
            # The raw white maximum is intentionally different; compare only black terms.
            x=row['decodes']['current_white']['after_black_estimation'];y=row['decodes']['fixed_white']['after_black_estimation']
            if x['black']!=y['black'] or x['cblack']!=y['cblack']:raise ValueError('Black changed between variants')
        row['LR_reference']={k:reference[k] for k in ('LR_file','LR_sha256','LR_WB','LR_T','LR_tint','LR_Y')}
        for values in row['models'].values():values['relative_mean_Y_to_LR_percent']=100*(values['mean_Y']/reference['LR_Y']-1)
    for path,digest in hashes.items():
        if native.sha256(Path(path))!=digest:raise ValueError('Input changed during experiment: '+path)
    result.update(campaign_id=campaign,schema_version=1,data_version=1,group='base-colorimetrique',
                  started_at_utc=now.isoformat(),inputs=[dict(file=p,sha256=h) for p,h in hashes.items()],
                  cohorts=aggregate(result['records']),
                  units=dict(Y='relative linear sRGB luminance',relative_error='100*(IPR/LR-1)',clipping='percent; differences in percentage points'),
                  method=dict(fresh_native_decodes_per_RAW=2,optics=False,restore_sidecars=False,production_modified=False,
                              white_only='Audited DNG raw white fixed before black subtraction; adjust_maximum_thr=0',
                              black='Existing IPR estimates unchanged; NOT DNG black substitution',
                              exposure='Audited per-scene camera EV added to existing DCP EV, before tables and curve',
                              output='Same sampled 16-bit sRGB diagnostic path as previous probes'),
                  limitations=['No pixel alignment, MAE or pixel DeltaE against LR',
                               'Mean luminance similarity does not certify physical colour accuracy',
                               'Current diagnostic output is not an IHM acceptance test',
                               'Calibration matrices unchanged; baseline tagging is not automatic approval to apply EV',
                               'Fixed central ROI is flower-specific only on 0081; other scenes not semantically segmented'])
    destination=Path('docs/validation/raw')/(campaign+'.json');native.write_immutable(destination,result)
    print(destination,flush=True)


if __name__=='__main__':main()
