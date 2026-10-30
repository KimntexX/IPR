"""Freeze a bench-only CFA Wiener prototype using authenticated development black crops."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import statistics
import subprocess
from audit_dark_mean import sha256


def summarize(cases):
    summary={}
    for branch in ('cfa_wiener','post_current','cfa_plus_post'):
        ratios=[];retention=[]
        for case in cases:
            noisy=case['noisy'];baseline=noisy['rcd_only']['rgb_mse_truth']
            if baseline>0:
                ratios.append(noisy[branch]['rgb_mse_truth']/baseline)
            clean=case['clean_controls'];base_gain=clean['rcd_only']['pattern_gain'];gain=clean[branch]['pattern_gain']
            if base_gain is not None and base_gain>0 and gain is not None:
                retention.append(gain/base_gain)
        summary[branch]=dict(median_mse_ratio_to_rcd_only=statistics.median(ratios),
            minimum_clean_pattern_gain_ratio=min(retention),clean_pattern_cases=len(retention),noisy_cases=len(ratios))
    comparisons=[case['noisy']['cfa_wiener']['rgb_mse_truth']/case['noisy']['post_current']['rgb_mse_truth'] for case in cases]
    median=statistics.median(comparisons);wins=sum(v<1 for v in comparisons)
    safe=summary['cfa_wiener']['minimum_clean_pattern_gain_ratio']>=.95
    summary['pre_vs_post_screen']=dict(median_mse_ratio=median,pre_better_cases=wins,total_cases=len(cases),
        all_clean_patterns_at_least_95_percent=safe,proceed_as_is=(median<1 and wins>=len(cases)/2 and safe))
    return summary


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pilot-proof',type=Path,required=True)
    p.add_argument('--tool',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();root=Path(__file__).resolve().parents[2]
    pilot=json.loads(args.pilot_proof.read_text(encoding='utf-8'))
    if pilot['verdict']!='pass_signed_module_contract_not_production_or_quality':
        raise ValueError('Pilot contract not qualified')
    selection=[pair for pair in pilot['pairs'] if pair['label'] in ('confirmed_dark_ISO1600','confirmed_dark_ISO51200')]
    if len(selection)!=2 or any(len(pair['captures'])!=2 for pair in selection):
        raise ValueError('Two confirmed development black pairs required')
    frozen_parent=root/pilot['freeze_path']
    if sha256(frozen_parent)!=pilot['freeze_sha256']:
        raise ValueError('Pilot freeze changed')
    parent=json.loads(frozen_parent.read_text(encoding='utf-8'))
    allowed={r['sha256'] for r in parent['selection'] if r['split']=='development' and r['category']=='confirmed_dark'}
    for pair in selection:
        for capture in pair['captures']:
            if capture['source']['sha256'] not in allowed or sha256(capture['path'])!=capture['sha256']:
                raise ValueError('Capture outside authenticated development black selection')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/cfa_denoise_prototype.cpp','tests/raw_bench/run_cfa_prototype.py',
             'src/raw/CameraDenoise.cpp','src/raw/CameraDenoise.h','src/raw/Demosaic.cpp','src/raw/Demosaic.h',
             'tests/raw_bench/CMakeLists.txt']
    freeze=dict(schema_version=1,created_utc=stamp,study='CFA_local_Wiener_prototype_freeze',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        pilot_proof_sha256=sha256(args.pilot_proof),pilot_freeze_sha256=sha256(frozen_parent),
        tool_sha256=sha256(args.tool),pairs=selection,
        sources={name:dict(sha256=sha256(root/name),text=(root/name).read_text(encoding='utf-8')) for name in sources},
        recipe=dict(crop='middle fixed 256px CFA crop',measurement_roi=[80,80,96,96],
            patterns=['flat','texture','colour_edge','line'],levels=[.005,.03],
            reference_noise='other development black, per-phase sample variance, both directions',
            prefilter='3x3 per phase, sample variance 9/8, gain=max(0,1-noisePower/localVariance), no signal clamp',
            variants=['rcd_only','cfa_wiener','post_current','cfa_plus_post'],post_settings=[100,100],
            controls='same variants on clean patterns with same reference variance',
            primary_metric='MSE versus known synthetic RGB; secondary versus noiseless RCD'),
        screening='Pursue pre unchanged only if median pre/post MSE<1, wins>=half cases, AND every clean structural gain>=95% of clean RCD.',
        limits=['Read-noise residual includes drift/fixed pattern; no shot noise, VST or DCP.',
                'External dark calibration is privileged versus blind post estimator; does not isolate placement alone.',
                'Small crops, one ROI, two development frames per ISO, one camera; no independent validation.',
                'Truth MSE includes intrinsic demosaic artefacts; clean-RCD secondary metric separate.',
                'No copied external implementation or patent-clearance claim.',
                'Patch timings do not qualify full-image/app RAM or the 20.8s production budget.'])
    frozen=root/'docs/validation/raw'/f'cfa-denoise-prototype-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    result=dict(schema_version=1,created_utc=stamp,study='CFA_local_Wiener_prototype',freeze_sha256=sha256(frozen),
                freeze_path=str(frozen.relative_to(root)),commands=[],pairs=[])
    failed=False
    try:
        run=subprocess.run([str(args.tool.resolve()),'--self-test'],env=env,cwd=root,capture_output=True,text=True,timeout=30)
        result['self_test']=dict(returncode=run.returncode,stdout=run.stdout,stderr=run.stderr)
        if run.returncode:
            raise RuntimeError('Native contract test failed')
        for pair in selection:
            command=[str(args.tool.resolve()),'--first',pair['captures'][0]['path'],'--second',pair['captures'][1]['path']]
            run=subprocess.run(command,env=env,cwd=root,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=180)
            record=dict(label=pair['label'],argv=command,returncode=run.returncode,stderr=run.stderr)
            result['commands'].append(record)
            if run.returncode:
                record['stdout']=run.stdout
                raise RuntimeError('Native prototype failed')
            measured=json.loads(run.stdout)
            if len(measured['cases'])!=16:
                raise ValueError('Missing expected pattern/level/direction cases')
            measured['label']=pair['label'];measured['summary']=summarize(measured['cases'])
            result['pairs'].append(measured)
            print(pair['label'],json.dumps(measured['summary']),flush=True)
        result['summary']=summarize([case for pair in result['pairs'] for case in pair['cases']])
    except Exception as exc:
        failed=True;result['error']=f'{type(exc).__name__}: {exc}'
    result['verdict']='fail' if failed else 'measured_prototype_only_not_production_validation'
    output=root/'docs/validation/raw'/f'cfa-denoise-prototype-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''))
    return int(failed)


if __name__=='__main__':
    raise SystemExit(main())
