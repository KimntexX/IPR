"""Build isolated dev/C3 engines and compare full neutral images, with measured budgets."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import sys

from capture import compare_captures,load_capture,sha256


def full_equal(a,b):
    return a['shape']==b['shape'] and a['dtype']==b['dtype'] and a['sha256']==b['sha256']


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','manifest','adobe-camera-root','reference-source','candidate-source','cmake','qt-dir','vcpkg','opencv-dir','opencv-bin'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--reference-revision',required=True);p.add_argument('--candidate-revision',required=True)
    a=p.parse_args();root=Path(__file__).resolve().parents[2];start=datetime.now(timezone.utc);stamp=start.strftime('%Y%m%dT%H%M%SZ')
    work=root/'build-patch_raw_bench/production'/stamp;work.mkdir(parents=True,exist_ok=False)
    state_path=work/'status.json'
    def status(**kw):
        payload=dict(started_at_utc=start.isoformat(),updated_at_utc=datetime.now(timezone.utc).isoformat(),**kw)
        temp=state_path.with_suffix('.tmp');temp.write_text(json.dumps(payload,indent=2),encoding='utf-8');temp.replace(state_path)
    records=[]
    try:
        inventory=json.loads(a.inventory.read_text(encoding='utf-8'));inputs=inventory['records']
        if len(inputs)!=30 or len({r['source_sha256'] for r in inputs})!=30:raise ValueError('Need frozen30 distinct RAWs')
        profiles={}
        for row in inputs:
            source=Path(row['source'])
            if sha256(source)!=row['source_sha256']:raise ValueError('Inventory source changed')
            model=row['model'];camera='Canon '+model
            profile=a.adobe_camera_root/camera/(camera+' Camera Neutral.dcp')
            if not profile.is_file():raise ValueError('Missing explicit Camera Neutral profile: '+str(profile))
            profiles[model]=dict(path=str(profile),sha256=sha256(profile))
        config_hash=sha256(a.manifest);engine_hashes={}
        for label,source in (('reference',a.reference_source),('candidate',a.candidate_source)):
            engine_hashes[label]={str(f.relative_to(source)):sha256(f) for f in (source/'src').rglob('*') if f.is_file() and f.suffix in ('.cpp','.h')}
        policy=dict(schema_version=1,campaign_id='raw-production-freeze-'+stamp,measured_at_utc=start.isoformat(),
            inventory=dict(path=str(a.inventory),sha256=sha256(a.inventory)),inputs=inputs,profiles=profiles,
            manifest=dict(path=str(a.manifest),sha256=config_hash),reference_revision=a.reference_revision,candidate_revision=a.candidate_revision,
            engine_sources=engine_hashes,decode_budget_ms=20800,
            scope='30 RAWs, two modes(CameraNeutral DCP and no profile), as-shot, optics off; fullRGBA row hashes plus3 diagnostic256 ROIs. Fresh process per image/mode/engine, alternating engine order; OS cache not flushed. Budget applies native decode only, peak working set measured before serialization, maximum8 RCD workers, OpenCV1 and OMP8. Hash mismatch fails even if ROIs match. No C4 or signed mode, automatic-WB/fallback/optics not certified.',
            source=Path(__file__).read_text(encoding='utf-8'),source_sha256=sha256(Path(__file__)))
        freeze=root/'docs/validation/raw'/f'gel-banc-raw-production-v001-{stamp}.json'
        with freeze.open('x',encoding='utf-8') as h:json.dump(policy,h,ensure_ascii=False,indent=2,allow_nan=False)
        print('FROZEN',freeze,flush=True)
        env=dict(os.environ);env['OMP_NUM_THREADS']='8';env['PATH']=os.pathsep.join([str(a.qt_dir.parents[2]/'bin'),str(a.vcpkg/'bin'),str(a.opencv_bin),env.get('PATH','')])
        tools={}
        for label,source,revision in (('reference',a.reference_source,a.reference_revision),('candidate',a.candidate_source,a.candidate_revision)):
            build=work/label
            commands=[[str(a.cmake),'-S',str(root/'tests/raw_bench/production'),'-B',str(build),
                '-DIPR_ENGINE_SOURCE='+str(source),'-DIPR_ENGINE_REVISION='+revision,'-DQt6_DIR='+str(a.qt_dir),
                '-DOpenCV_DIR='+str(a.opencv_dir),'-DCMAKE_PREFIX_PATH='+str(a.vcpkg),
                '-DLIBRAW_INCLUDE_DIR='+str(a.vcpkg/'include'),'-DLIBRAW_LIBRARY='+str(a.vcpkg/'lib/manual-link/raw.lib')],
                [str(a.cmake),'--build',str(build),'--config','Release','--target','iprProductionRawCapture','--parallel','2']]
            for index,command in enumerate(commands):
                status(status='running',phase=f'{label}-'+('configure' if index==0 else 'build'))
                with (work/f'{label}-{index}.log').open('x',encoding='utf-8') as log:
                    subprocess.run(command,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=1800)
            exe=build/'Release/iprProductionRawCapture.exe';tools[label]=dict(path=str(exe),sha256=sha256(exe))
            print('BUILT',label,flush=True)
        empty=work/'empty-profiles';empty.mkdir()
        for index,item in enumerate(inputs):
            source=Path(item['source']);profile=profiles[item['model']]
            for mode in ('dcp','matrix'):
                status(status='running',phase='decode',source=source.name,mode=mode,completed=len(records))
                outputs={};captures={}
                order=('reference','candidate') if index%2==0 else ('candidate','reference')
                for label in order:
                    out=work/f'{index:02}-{source.stem}-{mode}-{label}.json'
                    command=[tools[label]['path'],'--input',str(source),'--output',str(out),'--profile-root',str(empty),'--manifest',str(a.manifest)]
                    if mode=='dcp':command+=['--profile',profile['path']]
                    with (work/f'{index:02}-{mode}-{label}.log').open('x',encoding='utf-8') as log:
                        subprocess.run(command,cwd=work,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=240)
                    manifest,_=load_capture(out);outputs[label]=manifest;captures[label]=dict(path=str(out),sha256=sha256(out))
                ref,can=outputs['reference'],outputs['candidate']
                comparison=compare_captures(captures['reference']['path'],captures['candidate']['path'],('working_rec2020',))
                whole=full_equal(ref['full_image'],can['full_image']);metadata=ref['metadata']==can['metadata']
                budget=all(o['decode_ms']<=20800 and o['rcd_workers']<=8 and o['opencv_threads']<=1 for o in outputs.values())
                records.append(dict(source=item,mode=mode,captures=captures,full_image_equal=whole,metadata_equal=metadata,roi_comparison=comparison,
                    measurements={label:dict(decode_ms=o['decode_ms'],peak_working_set_bytes=o['decode_peak_working_set_bytes'],rcd_workers=o['rcd_workers'],opencv_threads=o['opencv_threads']) for label,o in outputs.items()},
                    full_images={label:o['full_image'] for label,o in outputs.items()},metadata={label:o['metadata'] for label,o in outputs.items()},
                    parity_pass=whole and metadata and comparison['verdict']=='pass',decode_budget_pass=budget))
                print(index+1,'/',len(inputs),source.name,mode,'parity',records[-1]['parity_pass'],'budget',budget,flush=True)
                if not records[-1]['parity_pass']:raise ValueError('Full-frame/metadata/ROI parity failure; stopped at first mismatch')
        if config_hash!=sha256(a.manifest) or any(sha256(Path(v['path']))!=v['sha256'] for v in profiles.values()):raise ValueError('Configuration changed')
        for label,source in (('reference',a.reference_source),('candidate',a.candidate_source)):
            if any(sha256(source/path)!=digest for path,digest in engine_hashes[label].items()):raise ValueError('Engine source changed')
        if any(sha256(Path(t['path']))!=t['sha256'] for t in tools.values()):raise ValueError('Engine tool changed')
        final='pass_scoped_C3' if len(records)==60 and all(r['parity_pass'] and r['decode_budget_pass'] for r in records) else 'fail'
        result=dict(schema_version=1,campaign_id='raw-production-'+stamp,measured_at_utc=datetime.now(timezone.utc).isoformat(),
            freeze=dict(path=str(freeze),sha256=sha256(freeze)),tools=tools,records=records,verdict=final,
            limitations='No C4/signed mode or all-format generalization. Full-image equality uses bytes hash, finite output required; ROI stats diagnostic only. Single sequential run per engine/mode/source, warm OS caches possible and build warming; no rigorous speedup confidence. Peak working set is per process, not whole-system RAM ceiling. No complete fallback, optics or cancellation campaign.')
        out=root/'docs/validation/raw'/f'banc-raw-production-v001-{datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")}.json'
        with out.open('x',encoding='utf-8') as h:json.dump(result,h,ensure_ascii=False,indent=2,allow_nan=False)
        status(status='succeeded' if final=='pass_scoped_C3' else 'failed',phase='complete',result=str(out),completed=len(records))
        print('RESULT',out,final,flush=True);return 0 if final=='pass_scoped_C3' else 1
    except Exception as error:
        status(status='failed',phase='error',error=str(error),completed=len(records))
        with (work/'partial.json').open('x',encoding='utf-8') as h:json.dump(dict(error=str(error),records=records),h,ensure_ascii=False,indent=2,allow_nan=False)
        raise


if __name__=='__main__':raise SystemExit(main())
