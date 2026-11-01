"""Bounded sequential photo pilot: extraction, pair qualification, fixed training.

No engine changes, source writes, TIFFs, automatic deletion, Git publication or
adaptive model search. Only frozen captures on the explicitly supplied card.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from audit_moon import digest
from qualify_raw_series import GROUPS


def save(path, value):
    with path.open('x', encoding='utf-8') as handle:
        json.dump(value, handle, indent=2, allow_nan=False)


def capture_selection():
    return [(group, scene, n) for group, scene, first, last in GROUPS
            for n in (range(first,last+1) if group in (2,7)
                      else sorted({first,(first+last)//2,last}))]


def checked_output(output):
    root=Path(__file__).resolve().parents[2]
    output=output.resolve()
    if not output.is_relative_to((root/'build-patch_neural_restoration').resolve()):
        raise ValueError('Use the dedicated neural build')
    return root,output


def run(args):
    root,work=checked_output(args.output)
    inventory=json.loads(args.inventory.read_text(encoding='utf-8'))
    records={Path(f['path']).stem:f for g in inventory['groups'] for f in g['frames']}
    selected=[dict(group=g,scene=s,source=records[f'6V1A{n:04d}'])
              for g,s,n in capture_selection()]
    for entry in selected:
        r=entry['source']; stat=Path(r['path']).stat()
        if (stat.st_size,stat.st_mtime_ns)!=(r['size_bytes'],r['mtime_ns']):
            raise ValueError('Source changed since CFA qualification')
    if shutil.disk_usage(root).free<8*2**30:
        raise ValueError('At least 8 GiB free required for bounded pilot')
    for path in (args.tool,args.profile,args.manifest,args.trainer):
        if not path.is_file():
            raise ValueError('Missing dependency: '+str(path))
    work.mkdir(parents=True,exist_ok=False)
    stamp=datetime.now(timezone.utc)
    freeze=dict(schema_version=1,data_version=1,
        campaign_id='neural-photo-pilot-v001-'+stamp.strftime('%Y%m%dT%H%M%SZ'),
        measured_at_utc=stamp.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        inventory=dict(path=str(args.inventory.resolve()),sha256=digest(args.inventory)),
        tool=dict(path=str(args.tool.resolve()),sha256=digest(args.tool)),
        profile=dict(path=str(args.profile.resolve()),sha256=digest(args.profile)),
        manifest=dict(path=str(args.manifest.resolve()),sha256=digest(args.manifest)),
        orchestrator_sha256=digest(__file__),
        selection='All 51 ISO100 reference frames; first/middle/last of each of six higher-ISO groups.',
        scene_partition=dict(indoor='train',vegetation='validation',test=None),
        training=dict(width=8,steps=256,batch=4,learning_rate=.001,seed=71,
                      patch_side=96,scale=2.,checkpoint_selection=False),
        qualification=dict(ecc_floor=.97,max_translation_pixels=12.,
            reference_lowpass_sigma=4.,reference_stability_dn_fraction=.001,
            fitting_rois=[0,2,4,6,8],monitoring_rois=[1,3,5,7],
            minimum_affine_gain=.1,maximum_affine_gain=16.,maximum_affine_offset=.05,
            unsaturated_sensor_fraction=.9,minimum_reference_valid_fraction=.5,
            minimum_patch_valid_fraction=.9,patch_origins=[24,144,264],
            tile_radiometry_floor=.003,tile_radiometry_noise_multiplier=6.,
            minimum_training_patches=32,minimum_validation_patches=16),
        user_lighting_context='User reports branches moving in front of the window and a low sun; cause plausible, not established.',
        sources=selected)
    save(work/'freeze.json',freeze)
    started=time.monotonic(); captures=[]
    env=dict(os.environ);env['OMP_NUM_THREADS']='2';env['OPENBLAS_NUM_THREADS']='2'
    for g in ('C:/Qt/6.11.2/msvc2022_64/bin','C:/opencv/build/x64/vc16/bin',
              'C:/dev/vcpkg/installed/x64-windows/bin'):
        if Path(g).is_dir(): env['PATH']=g+os.pathsep+env.get('PATH','')
    try:
        for index, entry in enumerate(selected):
            record=entry['source']; source=Path(record['path'])
            destination=work/(source.stem+'.json')
            with (work/(source.stem+'.log')).open('x',encoding='utf-8') as log:
                subprocess.run([str(args.tool.resolve()),'--input',str(source.resolve()),
                    '--output',str(destination),'--profile',str(args.profile.resolve()),
                    '--manifest',str(args.manifest.resolve())],cwd=root,env=env,
                    stdout=log,stderr=subprocess.STDOUT,check=True,timeout=180)
            captured=json.loads(destination.read_text(encoding='utf-8'))
            if captured['source_sha256']!=record['sha256'] or not captured['calibration_applied']:
                raise ValueError('Wrong source/calibration')
            if captured['profile_sha256']!=freeze['profile']['sha256'] or captured['manifest_sha256']!=freeze['manifest']['sha256']:
                raise ValueError('Profile/calibration changed')
            captured.update(group=entry['group'],scene=entry['scene'],descriptor=str(destination),
                            descriptor_sha256=digest(destination))
            captures.append(captured)
            print(f'CAPTURE {index+1}/{len(selected)} {source.name}',flush=True)
        if digest(args.tool)!=freeze['tool']['sha256']:
            raise ValueError('Extraction executable changed')
        save(work/'captures.json',dict(freeze_sha256=digest(work/'freeze.json'),records=captures,
                                     elapsed_seconds=time.monotonic()-started))
        # Imports happen after extraction; separate calculation/I/O modules.
        from qualify_photo_pairs import prepare
        dataset=prepare(work,freeze,captures)
        if dataset['status']!='qualified_for_exploratory_pilot':
            print('STOP qualification insufficient; see pairs.json',flush=True)
            return
        with (work/'training.log').open('x',encoding='utf-8') as log:
            subprocess.run([str(args.trainer.resolve()),str(root/'tests/neural/train_photo_pilot.py'),
                '--work',str(work)],cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,
                check=True,timeout=1800)
        print('COMPLETE '+str(work/'training.json'),flush=True)
    except Exception as error:
        save(work/'failure.json',dict(error=str(error),completed_captures=len(captures),
                                     elapsed_seconds=time.monotonic()-started))
        raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('inventory','tool','profile','manifest','trainer','output'):
        parser.add_argument('--'+key,type=Path,required=True)
    run(parser.parse_args())
