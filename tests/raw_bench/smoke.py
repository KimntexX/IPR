"""Small real-RAW unpack capture/readback check; not candidate-development parity."""
from datetime import datetime,timezone
import argparse
import json
import os
from pathlib import Path
import subprocess

import numpy as np
import rawpy

from capture import load_capture,sha256


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',action='append',type=Path,required=True)
    p.add_argument('--qt-bin',type=Path,required=True);p.add_argument('--libraw-bin',type=Path,required=True);args=p.parse_args()
    root=Path(__file__).resolve().parents[2];stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    work=root/'build-patch_raw_bench'/'smoke'/stamp;work.mkdir(parents=True,exist_ok=False)
    tool=root/'build-patch_raw_bench/Release/iprRawUnpackCapture.exe'
    env=dict(os.environ);env['PATH']=os.pathsep.join((str(args.qt_bin),str(args.libraw_bin),env.get('PATH','')))
    records=[]
    for index,source in enumerate(args.input):
        out=work/f'{index}-{source.stem}.json'
        subprocess.run([str(tool),'--input',str(source),'--output',str(out)],check=True,env=env,timeout=120)
        manifest,stages=load_capture(out);matches=[]
        with rawpy.imread(str(source)) as raw:
            for (_,origin),(stage,values) in stages.items():
                y,x=origin;h,w,_=stage['shape'];reference=raw.raw_image_visible[y:y+h,x:x+w]
                if reference.shape!=(h,w):raise ValueError('RAW reader geometry differs')
                matches.append(dict(origin_yx=list(origin),equal=bool(np.array_equal(values[:,:,0],reference)),
                    differing_values=int(np.count_nonzero(values[:,:,0]!=reference))))
        records.append(dict(source=str(source),source_sha256=manifest['source']['sha256'],capture=str(out),capture_sha256=sha256(out),
            model=manifest['metadata']['model'],libraw_version=manifest['libraw_version'],rois=matches))
        print(source.name,manifest['metadata']['model'],matches,flush=True)
    now=datetime.now(timezone.utc);output=root/'docs/validation/raw'/f'banc-raw-unpack-smoke-v001-{now.strftime("%Y%m%dT%H%M%SZ")}.json'
    result=dict(schema_version=1,campaign_id='raw-unpack-smoke-'+stamp,measured_at_utc=now.isoformat(),records=records,
        rawpy_version=rawpy.__version__,rawpy_libraw_version=list(rawpy.libraw_version),tool_sha256=sha256(tool),
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        sources={n:dict(sha256=sha256(root/n),source=(root/n).read_text(encoding='utf-8')) for n in ('tests/raw_bench/smoke.py','tests/raw_bench/capture.py','tests/raw_bench/unpack_capture.cpp','tests/raw_bench/CMakeLists.txt')},
        verdict='pass' if all(r['equal'] for s in records for r in s['rois']) else 'fail',
        limitations='Native LibRaw unpack vs rawpy LibRaw unpack readback on explicit ROIs only. Shared decoder family, not independent ground truth. No candidate-development comparison, signed-mode validation, performance qualification or complete format coverage.')
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print('RESULT',output,flush=True)
    return 0 if result['verdict']=='pass' else 1


if __name__=='__main__':raise SystemExit(main())
