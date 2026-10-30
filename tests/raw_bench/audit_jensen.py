"""Evaluate the actual fixed DCP at stored camera means; no repeated denoising campaign."""
import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import subprocess
from audit_dark_mean import sha256


def decomposition(states, metric):
    a,b=states['B0'],states['B100']
    before=a['mean_of_F'][metric];after=b['mean_of_F'][metric]
    f0=a['F_of_mean'][metric];f1=b['F_of_mean'][metric]
    if not all(math.isfinite(v) for v in (before,after,f0,f1)):
        raise ValueError('Nonfinite Jensen input')
    observed=after-before
    shift=f1-f0
    gap=(after-f1)-(before-f0)
    if abs(observed-shift-gap)>1e-9:
        raise ValueError('Mean decomposition does not close')
    return dict(observed_delta=observed,mean_shift_component=shift,
                distribution_gap_component=gap,gap_before=before-f0,gap_after=after-f1)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--project-root',type=Path,required=True)
    p.add_argument('--profile-root',type=Path,required=True)
    p.add_argument('--parent',type=Path,action='append',required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args()
    root=Path(__file__).resolve().parents[2]
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(p.resolve()) for p in args.runtime_dir)+os.pathsep+env.get('PATH','')
    proof=dict(schema_version=1,created_utc=stamp,study='fixed_DCP_Jensen_gap_audit',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        tool_sha256=sha256(args.tool),parents={str(p):sha256(p) for p in args.parent},
        source=dict(sha256=sha256(__file__),text=Path(__file__).read_text(encoding='utf-8')),
        native_source=dict(sha256=sha256(root/'tests/raw_bench/camera_denoise_leakage.cpp'),
                           text=(root/'tests/raw_bench/camera_denoise_leakage.cpp').read_text(encoding='utf-8')),
        scenes=[],limits=['Existing camera and output means reused, no new denoising or mask selection.',
                         'Complete pointwise DCP/render chain F, not an isolated scalar tone curve.',
                         'Jensen gaps describe distribution effects, not a clean truth or causal component ablation.',
                         'Mean vector rendered in float32; extremely small gaps are not meaningful.',
                         'Unchanged pre-D0 worktree; no claim about a different engine head.'])
    failed=False
    try:
        for filename in ('7Y6A7599.CR3','6V1A9153.CR3','IMG_3135.CR2'):
            command=[str(args.tool.resolve()),'--input',str(args.project_root/'tests/image/base'/filename),
                     '--manifest',str(root/'config/raw-normalization.json'),
                     '--profile-root',str(args.profile_root.resolve()),'--label','jensen-gap']
            for parent in args.parent:
                command.extend(['--jensen-proof',str(parent.resolve())])
            run=subprocess.run(command,cwd=root,env=env,capture_output=True,text=True,
                               encoding='utf-8',errors='replace',timeout=180)
            record=dict(scene=filename,argv=command,returncode=run.returncode,stderr=run.stderr)
            proof['scenes'].append(record)
            if run.returncode:
                record['stdout']=run.stdout
                raise RuntimeError('Native Jensen audit failed')
            measured=json.loads(run.stdout);record['measurements']=measured
            for population in measured['populations']:
                population['decomposition']={metric:decomposition(population['states'],metric) for metric in
                    ('linear_rec2020_y_mean','signed_encoded_y_mean_255','clipped_encoded_y_mean_255')}
                print(filename,population['parent_study'],population['decomposition']['clipped_encoded_y_mean_255'],flush=True)
    except Exception as exc:
        failed=True;proof['error']=f'{type(exc).__name__}: {exc}'
    proof['verdict']='fail' if failed else 'measured_not_causal_or_quality_validation'
    output=root/'docs/validation/raw'/f'camera-denoise-jensen-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,proof['verdict'],proof.get('error',''))
    return int(failed)


if __name__=='__main__':
    raise SystemExit(main())
