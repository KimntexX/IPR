"""C2 parity on explicitly observable boundaries; never certifies missing DCP."""
from datetime import datetime,timezone
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

import numpy as np

from capture import compare_captures,load_capture,sha256

COMMON=('unpacked_cfa','pre_demosaic_cfa','post_demosaic_camera_rgb','pre_output_camera_rgb','post_median_camera_rgb','pre_dcp_camera_rgb')
CANDIDATE=COMMON+('black_subtracted_cfa','normalized_cfa')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',action='append',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    p.add_argument('--calibration-captures',type=Path,help='Immutable production captures providing explicit black/white for matching sources only')
    args=p.parse_args()
    root=Path(__file__).resolve().parents[2];start=datetime.now(timezone.utc);stamp=start.strftime('%Y%m%dT%H%M%SZ')
    tool=root/'build-patch_raw_bench/Release/iprRawDevelopmentCapture.exe';tool_hash=sha256(tool)
    work=root/'build-patch_raw_bench/parity'/stamp;work.mkdir(parents=True,exist_ok=False)
    env=dict(os.environ);env['PATH']=os.pathsep.join([str(d) for d in args.runtime_dir]+[env.get('PATH','')])
    freeze=root/'docs/validation/raw'/f'gel-banc-raw-parite-v001-{stamp}.json'
    identities=[dict(path=str(s.resolve()),sha256=sha256(s)) for s in args.input]
    calibration=None;calibration_reference=None
    if args.calibration_captures:
        calibration=json.loads(args.calibration_captures.read_text(encoding='utf-8'))['native']
        calibration_reference=dict(path=str(args.calibration_captures.resolve()),sha256=sha256(args.calibration_captures))
    if len({i['path'] for i in identities})!=len(identities):raise ValueError('Duplicate inputs')
    policy=dict(schema_version=1,campaign_id='raw-parity-freeze-'+stamp,measured_at_utc=start.isoformat(),inputs=identities,required_stages=list(COMMON),
        candidate_required_stages=list(CANDIDATE),tolerances='Exactly zero, no floating tolerance approved',
        mode='parity',calibration_reference=calibration_reference,scope='C2 module with native LibRaw levels unless explicit previous production captures match source; WB as-shot, sensor orientation, no automatic IPR calibration selection or DCP; no visual or signed-mode validation',
        tool_sha256=tool_hash,source=Path(__file__).read_text(encoding='utf-8'),source_sha256=sha256(Path(__file__)))
    with freeze.open('x',encoding='utf-8') as f:json.dump(policy,f,ensure_ascii=False,indent=2,allow_nan=False)
    print('FROZEN',freeze,flush=True);records=[]
    for index,item in enumerate(identities):
        source=Path(item['path']);captures={}
        if sha256(source)!=item['sha256']:raise ValueError('Source changed before capture')
        level_args=[];levels=None
        if calibration and str(source) in calibration:
            old=calibration[str(source)]['record']
            if old['source_sha256']!=item['sha256']:raise ValueError('Calibration source identity mismatch')
            levels=dict(black=old['applied_black_dn'],white=old['white_before_process'])
            level_args=['--black',','.join(str(v) for v in levels['black']),'--white',str(levels['white'])]
        for kind in ('reference','candidate'):
            out=work/f'{index:03}-{source.stem}-{kind}.json'
            subprocess.run([str(tool),'--input',str(source),'--output',str(out),'--path',kind,'--mode','parity']+level_args,env=env,check=True,timeout=240)
            captures[kind]=out
        result=compare_captures(captures['reference'],captures['candidate'],COMMON)
        manifest,stages=load_capture(captures['candidate']);present={key[0] for key in stages}
        if set(CANDIDATE)-present:raise ValueError('Candidate observer did not emit all expected stages')
        if any(not np.isfinite(values).all() for _,values in stages.values()):raise ValueError('Nonfinite candidate observer capture')
        out=work/f'{index:03}-comparison.json'
        with out.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
        records.append(dict(source=item,captures={k:dict(path=str(v),sha256=sha256(v)) for k,v in captures.items()},comparison=dict(path=str(out),sha256=sha256(out)),
            verdict=result['verdict'],explicit_levels=levels,max_abs=max((r['max_abs'] for r in result['stages'] if 'max_abs' in r),default=None),
            observed_candidate_stages=sorted(present),iso=manifest['metadata']['iso'],model=manifest['metadata']['model'],
            negative_before_clipping=[dict(origin_yx=list(key[1]),fraction=float(np.mean(values<0))) for key,(_,values) in stages.items() if key[0]=='black_subtracted_cfa']))
        print(index+1,'/',len(identities),source.name,result['verdict'],'max',records[-1]['max_abs'],flush=True)
        if result['verdict']!='pass':break
    if sha256(tool)!=tool_hash:raise ValueError('Tool changed during parity campaign')
    now=datetime.now(timezone.utc);out=root/'docs/validation/raw'/f'banc-raw-parite-v001-{now.strftime("%Y%m%dT%H%M%SZ")}.json'
    result=dict(schema_version=1,campaign_id='raw-parity-'+stamp,measured_at_utc=now.isoformat(),started_at_utc=start.isoformat(),completed_at_utc=now.isoformat(),freeze=dict(path=str(freeze),sha256=sha256(freeze)),records=records,
        verdict='pass_scoped_C2' if len(records)==len(identities) and all(r['verdict']=='pass' for r in records) else 'fail',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        sources={n:dict(sha256=sha256(root/n),source=(root/n).read_text(encoding='utf-8')) for n in ('tests/raw_bench/parity.py','tests/raw_bench/development_capture.cpp','tests/raw_bench/capture.py','src/raw/RawDevelopment.cpp','src/raw/Demosaic.cpp','src/raw/LibRawInput.cpp')},
        calibration_reference=calibration_reference,
        limitations='Only fixed3 ROIs256 per stage, not full frame parity. Reference/candidate share current IPR RCD and input adapter. LibRaw native black/white, not IPR calibrated production levels. Candidate signed and normalized stages observed but no independent LibRaw boundary for them; not falsely reconstructed. No DCP, orientation, optics, automatic WB/fallbacks, private-corpus CI or performance qualification. Original production code unchanged in this bench.')
    with out.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print('RESULT',out,flush=True)
    return 0 if result['verdict']=='pass_scoped_C2' else 1


if __name__=='__main__':raise SystemExit(main())
