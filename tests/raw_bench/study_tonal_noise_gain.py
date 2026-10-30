"""Bench-only tonal derivative and residual reuse feasibility; no RAW rendering or local denoise policy."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
from audit_dark_mean import sha256


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();root=Path(__file__).resolve().parents[2]
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/tonal_gain_probe.cpp','tests/raw_bench/study_tonal_noise_gain.py',
        'src/core/Contrast.cpp','src/core/ToneRecovery.cpp','src/core/ToneStatistics.cpp','src/raw/CameraDenoise.cpp']
    proof=dict(schema_version=2,created_utc=stamp,study='tonal_noise_gain_and_residual_reuse',
        supersedes='camera-denoise-tonal-gain-v001-20261030T131638Z.json',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        delivered_reference='b19b4c3 / v0.1.63-alpha',tool_sha256=sha256(args.tool),
        sources={name:dict(sha256=sha256(root/name),text=(root/name).read_text(encoding='utf-8')) for name in sources},
        limits=['Neutral-grey transfer only, typical scene statistics, no DCP, no spatial/correlated chroma model.',
            'RAW exposure precedes the profile curve: these raster-like transfer factors are not full RAW factors.',
            'First-order noise propagation needs local Jacobian and covariance in real RGB pipeline.',
            'Residual reuse checked on one small synthetic image, not bit-exact or a production cache qualification.',
            'No tone-aware weights applied; no benefit, halos, preview latency, full image budget or cache RAM validated.'])
    try:
        result=subprocess.run([str(args.tool.resolve())],cwd=root,env=env,capture_output=True,text=True,
            encoding='utf-8',errors='replace',timeout=120)
        proof['command']=dict(returncode=result.returncode,stderr=result.stderr)
        if result.returncode:
            proof['command']['stdout']=result.stdout
            raise RuntimeError('Native tonal study failed')
        proof['measurements']=json.loads(result.stdout)
        proof['verdict']='measured_feasibility_not_production_or_quality_validation'
    except Exception as exc:
        proof['error']=f'{type(exc).__name__}: {exc}'
        proof['verdict']='fail'
    output=root/'docs/validation/raw'/f'camera-denoise-tonal-gain-v002-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(proof,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,proof['verdict'],proof.get('error',''))
    return int(proof['verdict']=='fail')


if __name__=='__main__':
    raise SystemExit(main())
