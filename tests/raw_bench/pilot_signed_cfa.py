"""Qualify signed module captures on development-only corpus; not production calibration or a denoiser."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import os
import numpy as np
from audit_dark_mean import sha256
from capture import load_capture
from signed_measurement import moments, planes, REQUIRED_STAGES


def neighbour_correlations(values):
    out={}
    for name,a,b in (('x',values[:,:-1],values[:,1:]),('y',values[:-1],values[1:])):
        correlations=[]
        for c in range(values.shape[2]):
            left=a[...,c].ravel();right=b[...,c].ravel()
            correlations.append(float(np.corrcoef(left,right)[0,1]) if left.std()>0 and right.std()>0 else None)
        out[name]=correlations
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inventory',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    inventory=json.loads(args.inventory.read_text(encoding='utf-8'))
    selection=inventory['pilot']
    if len(selection)!=6 or any(r['split']!='development' for r in selection):
        raise ValueError('Expected six development-only inputs, never validation or sealed')
    groups={}
    for r in selection:
        if sha256(r['path'])!=r['sha256']:
            raise ValueError('Input hash mismatch before pixels')
        key=f"{r['category']}_ISO{r['metadata']['iso']}"
        groups.setdefault(key,[]).append(r)
    if len(groups)!=3 or any(len(v)!=2 for v in groups.values()):
        raise ValueError('Expected three declared pairs')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['src/raw/RawDevelopment.cpp','src/raw/Demosaic.cpp','src/raw/LibRawInput.cpp',
             'tests/raw_bench/development_capture.cpp','tests/raw_bench/pilot_signed_cfa.py']
    freeze=dict(schema_version=1,created_utc=stamp,study='signed_cfa_module_pilot_freeze',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        inventory_sha256=sha256(args.inventory),tool_sha256=sha256(args.tool),selection=selection,
        sources={name:dict(sha256=sha256(root/name)) for name in sources},
        source=Path(__file__).read_text(encoding='utf-8'),
        policy='signed',black_dn_rg_bg2=[2048]*4,white_dn=16383,
        scope='Explicit fixed DN levels for module qualification, NOT production black/white selection or DCP.',
        limits=['ROI positions from existing capture tool, not cherry-picked after measurement.',
                'Old noise campaign already measured these sources; not a new holdout.',
                'No sealed or historical control frames decoded.',
                'CFA phases separate; pair difference cancels common structure, not a total noise oracle.',
                'No a*signal+b fitting, VST, image denoiser or quality improvement tested.'])
    frozen=root/'docs/validation/raw'/f'cfa-signed-pilot-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    env=os.environ.copy()
    env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    folder=root/'build-patch_raw_bench'/f'cfa-signed-pilot-{stamp}'
    folder.mkdir()
    result=dict(schema_version=1,created_utc=stamp,study='signed_cfa_module_pilot',freeze_sha256=sha256(frozen),
                freeze_path=str(frozen.relative_to(root)),scope=freeze['scope'],pairs=[],commands=[])
    failed=False
    try:
        for label,pair in groups.items():
            loaded=[]
            for i,record in enumerate(pair):
                target=folder/f'{label}-{i}.json'
                command=[str(args.tool.resolve()),'--input',record['path'],'--output',str(target),
                         '--path','candidate','--mode','signed','--black','2048,2048,2048,2048','--white','16383']
                run=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,
                                   encoding='utf-8',errors='replace',timeout=180)
                result['commands'].append(dict(argv=command,returncode=run.returncode,stdout=run.stdout,stderr=run.stderr))
                if run.returncode:
                    raise RuntimeError('Capture failed')
                manifest,stages=load_capture(target)
                if manifest['source']['sha256']!=record['sha256'] or manifest['mode']!='signed':
                    raise ValueError('Captured input/policy mismatch')
                if {k[0] for k in stages}!=REQUIRED_STAGES:
                    raise ValueError('Missing intermediate stage')
                for key in stages:
                    if key[0]=='pre_output_camera_rgb':
                        x=stages[key][1]
                        for stage in ('post_median_camera_rgb','pre_dcp_camera_rgb'):
                            if stages[(stage,key[1])][1].tobytes()!=x.tobytes():
                                raise ValueError('Signed float output changed after pre-output capture')
                loaded.append((manifest,stages,target))
            if set(loaded[0][1])!=set(loaded[1][1]):
                raise ValueError('Pair stages/geometries differ')
            row=dict(label=label,captures=[dict(path=str(v[2]),sha256=sha256(v[2]),
                source=v[0]['source'],applied=v[0]['applied'],metadata=v[0]['metadata'],libraw_version=v[0]['libraw_version']) for v in loaded],measurements=[])
            for key in sorted(loaded[0][1]):
                da,a=loaded[0][1][key];db,b=loaded[1][1][key]
                for field in ('shape','dtype','unit','layout','origin_yx','context'):
                    if da[field]!=db[field]:
                        raise ValueError('Pair geometry/units/input metadata differ')
                for (name,x),(name2,y) in zip(planes(da,a),planes(db,b)):
                    if name!=name2:
                        raise ValueError('CFA plane mismatch')
                    difference=(x.astype(np.float64)-y.astype(np.float64))/np.sqrt(2.)
                    row['measurements'].append(dict(stage=key[0],origin_yx=list(key[1]),plane=name,unit=da['unit'],
                        first=moments(x),second=moments(y),difference_over_sqrt2=moments(difference),
                        differential_neighbour_correlations=neighbour_correlations(difference)))
            result['pairs'].append(row)
            print(label,'measured',flush=True)
    except Exception as exc:
        failed=True;result['error']=f'{type(exc).__name__}: {exc}'
    result['verdict']='fail' if failed else 'pass_signed_module_contract_not_production_or_quality'
    output=root/'docs/validation/raw'/f'cfa-signed-pilot-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''))
    return int(failed)


if __name__=='__main__':
    raise SystemExit(main())
