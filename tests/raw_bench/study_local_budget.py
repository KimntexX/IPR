"""Paired routing ablation; reuse frozen inputs, profiles and prior filter hashes."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import cv2
import numpy as np
from audit_dark_mean import sha256
from generate_noise_oracle import instrument
from spectral_read_model import prepare,draw
from study_spectral_budget import calibration_tiles,DOMAINS
from study_spatial_budget import read_residual
from study_population_filter import fixture,gates as previous_gates
from camera_noise_budget import mosaic,simulate_context
from study_signal_filter import encoded_rgb
from study_noise_model_ablation import invoke
from transport_batch import native_batch

ROUTES=('cap','fallback')


def gates(rows):
    result={}
    for route in ROUTES:
        mapped=[]
        for row in rows:
            models={}
            for candidate,measurement in row['measurements'].items():
                models[candidate]=dict(measurement,external_covariance_noisy=measurement[f'external_{route}_noisy'],
                    external_covariance_clean_stress=measurement[f'external_{route}_clean_stress'])
            mapped.append(dict(row,measurements=models))
        checks=previous_gates(mapped)
        for domain in DOMAINS:
            for candidate in ('spectral','bank'):
                subset=[r for r in rows if r['domain']==domain and r['pattern']!='flat']
                losses=[r['measurements'][candidate]['noisy_100']['pattern_gain']-
                    r['measurements'][candidate][f'external_{route}_noisy']['pattern_gain'] for r in subset]
                value=checks[domain][candidate]
                value['worst_noisy_gain_loss']=max(losses)
                value['checks']['noisy_gain_loss_le_002']=bool(max(losses)<=.02)
                value['passed']=all(value['checks'].values())
        result[route]=checks
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('parent','transport-tool','oracle-tool','generated-source'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();cv2.setNumThreads(1);root=Path(__file__).resolve().parents[2]
    parent=json.loads(args.parent.read_text(encoding='utf-8'))
    parent_freeze=root/parent['freeze_path']
    if sha256(parent_freeze)!=parent['freeze_sha256']:raise ValueError('Changed parent freeze')
    frozen=json.loads(parent_freeze.read_text(encoding='utf-8'))
    population_path=Path(frozen['parent']['path'])
    if sha256(population_path)!=frozen['parent']['sha256']:raise ValueError('Changed population proof')
    population=json.loads(population_path.read_text(encoding='utf-8'));selection_path=root/population['freeze_path']
    if sha256(selection_path)!=population['freeze_sha256']:raise ValueError('Changed source selection')
    selection=json.loads(selection_path.read_text(encoding='utf-8'))
    if args.generated_source.read_text(encoding='utf-8')!=instrument((root/'src/raw/CameraDenoise.cpp').read_text(encoding='utf-8')):raise ValueError('Instrument mismatch')
    for name in ('src/raw/CameraDenoise.cpp','src/raw/Demosaic.cpp'):
        if sha256(root/name)!=frozen['sources'][name]['sha256']:raise ValueError('Changed engine')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/'+s for s in ('study_local_budget.py','study_population_filter.py','generate_noise_oracle.py',
        'structure_baseline_probe.cpp','noise_bench_control.h','noise_bench_control.cpp')]+['src/raw/CameraDenoise.cpp']
    freeze=dict(schema_version=1,created_utc=stamp,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(args.parent),sha256=sha256(args.parent)),sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        tools=dict(oracle=sha256(args.oracle_tool),transport=sha256(args.transport_tool)),
        threads=dict(host_logical_hint=os.cpu_count(),process_available_hint=os.process_cpu_count() if hasattr(os,'process_cpu_count') else None,
            current_policy='max(1,min(8,host>1?host-1:1)), caller included; tile count further reduces actual workers; not a global budget across jobs.',nested_blas_threads=1),
        protocol=dict(expected_cases=64,settings=[100,100],routes=ROUTES,
            cap='Per-scale diagonal sigma ceiling from external profile applied to pooled and128 conditional samples. Keep correlation, support, transition and structural gating; no sigma increase.',
            fallback='Replace pooled fallback only; conditional values/support unchanged. Ablation, not automatic better routing.',
            gates='Retain ALL previous per-domain gates, plus every noisy-detail projection loss<=.02 vs current. No retuning.',
            parity='Every CFA/observed/truth input and baseline/constant-external/zero output must match previous campaign hashes exactly.'),
        limits=frozen['limits']+['Paired ablation of already-seen64cases, not fresh data or independent validation.',
            'This preserves conditional routing but still uses a constant external ROI profile; not a full pixelwise physical noise map.',
            'Cap is diagonal only, retains current correlation; it does not inject cross-spectrum as a new correlation model.',
            'Hardware concurrency is a hint, not idle cores; cap8 retained for scientific comparability, no production thread policy changed.'])
    path=root/'docs/validation/raw'/f'local-budget-freeze-v001-{stamp}.json'
    with path.open('x',encoding='utf-8') as f:json.dump(freeze,f,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,data_version=1,created_utc=stamp,freeze_path=str(path.relative_to(root)),freeze_sha256=sha256(path),rows=[],parities=0)
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(d.resolve()) for d in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started=time.monotonic()
    try:
        for entry in population['models']:
            iso=entry['iso'];context=entry['context'];pair=next(p for p in selection['selection'] if p['label'].endswith(str(iso)))
            paths=[(c['path'],c['source']['sha256']) for c in pair['captures']]
            extra=next(c for c in population['captures'] if c['source']['sha256']==selection['extras'][str(iso)]['sha256'])
            paths.append((extra['path'],extra['source']['sha256']))
            model=prepare([t for path,digest in paths for t in calibration_tiles(path,context,digest)],context)
            real=[read_residual(c['path'],context,c['source']['sha256'])[0] for c in selection['real_controls'] if c['metadata']['iso']==iso]
            for prior in (r for r in parent['rows'] if r['iso']==iso):
                seed=prior['seed'];target=mosaic(fixture(iso,prior['pattern']))
                truth=native_batch(args.transport_tool,[target],env)[0][0]
                read=draw(model,seed,iso) if prior['domain']=='spectral_internal' else real[seed%2]
                sample=simulate_context(context,target,read,np.random.default_rng(np.random.SeedSequence([seed,iso])),prior['distribution'])
                observed,identity=native_batch(args.transport_tool,[sample],env)[0]
                if identity!=prior['transport']:raise ValueError('Changed transport input/output')
                payload={};hashes={}
                for name,array in (('truth',truth),('observed',observed),('reference',np.full((256,256,3),.03))):payload[name],hashes[name]=encoded_rgb(array)
                if hashes!=prior['input_sha256']:raise ValueError('Changed RGB input')
                measurements={}
                for candidate,covariance in prior['covariance_profiles'].items():
                    measured=invoke(args.oracle_tool,dict(payload,covariance_profile=covariance,routing_ablation=True),env)
                    for key in ('noisy_100','clean_100','external_covariance_noisy','external_covariance_clean_stress','zero_model_clean'):
                        if measured[key]['pixels_sha256']!=prior['measurements'][candidate][key]['pixels_sha256']:raise ValueError('Changed original branch')
                        result['parities']+=1
                    measurements[candidate]=measured
                result['rows'].append({**{k:prior[k] for k in ('iso','domain','distribution','pattern','seed','input_rgb_mse')},
                    'input_sha256':hashes,'transport':identity,'measurements':measurements})
                print(len(result['rows']),'/64',iso,prior['domain'],prior['pattern'],flush=True)
        if len(result['rows'])!=64 or result['parities']!=640:raise ValueError('Incomplete paired ablation')
        result['gates']=gates(result['rows']);result['verdict']='measured_local_routing_ablation_not_production_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'local-budget-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':raise SystemExit(main())
