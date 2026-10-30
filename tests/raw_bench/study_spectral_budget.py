"""Population read-noise budget versus a frozen single-frame conditional target.

Two domains are never pooled: internal spectral simulation and real-read replay.
Neither substitutes for independent photos/camera characterization.
"""
import argparse
from datetime import datetime,timezone
import hashlib
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
from capture import load_capture
from audit_signal_noise_bridge import PHASES
from camera_noise_budget import prepare_context,mosaic,simulate_context,local_mean
from study_structure_prediction import authenticated_selection
from study_spatial_budget import fixture,ROIS,PATTERNS,read_residual,ensemble,compare_draws
from spectral_read_model import prepare,draw
from transport_batch import native_batch
from audit_spatial_budget import summary

CALIBRATION=tuple(range(110001,110033))
CONTROL=tuple(range(130001,130033))
GUIDE=101001
DOMAINS=('spectral_internal','real_read_replay')


def gates(rows):
    result={}
    for domain in DOMAINS:
        group=[r for r in rows if r['domain']==domain]
        values=lambda label,flat:[v for r in group if (r['pattern']=='flat')==flat for z in r['regions'].values() for v in z['models'][label]['comparison']]
        flat=summary(values('spatial',True));nonflat=summary(values('spatial',False));old=summary(values('flat',False))
        all_values=values('spatial',True)+values('spatial',False);ratios=np.array([v['sigma_predicted_over_observed'] for v in all_values])
        checks=dict(flat_median_covariance_le_015=flat['covariance_error_percentiles_50_95_100'][0]<=.15,
                    every_sigma_ratio_in_08_125=bool(ratios.min()>=.8 and ratios.max()<=1.25),
                    transition_median_improves_flat=nonflat['covariance_error_percentiles_50_95_100'][0]<old['covariance_error_percentiles_50_95_100'][0])
        result[domain]=dict(checks=checks,passed=all(checks.values()),flat=flat,spatial_transition=nonflat,flat_transition=old,
                            sigma_min=float(ratios.min()),sigma_max=float(ratios.max()))
    return result


def calibration_tiles(path,context,source_hash):
    manifest,stages=load_capture(path)
    if manifest['source']['sha256']!=source_hash:raise ValueError('Changed calibration source')
    keys=sorted([k for k in stages if k[0]=='unpacked_cfa'],key=lambda k:k[1][1])
    if len(keys)!=3:raise ValueError('Need three frozen spatial crops per physical frame')
    output=[]
    for key in keys:
        descriptor,array=stages[key]
        if descriptor['context']['cfa']!=[[0,1],[3,2]] or array.shape!=(256,256,1):raise ValueError('Unexpected spectral crop')
        field=array[...,0].astype(float)
        for phase,(dy,dx,_) in PHASES.items():field[dy::2,dx::2]-=context['phases'][phase]['reference_black']
        output.append(field)
    return output


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','pilot-proof','bridge-proof','spatial-proof','transport-tool','unpack-tool'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();root=Path(__file__).resolve().parents[2];cv2.setNumThreads(1)
    inventory=json.loads(args.inventory.read_text(encoding='utf-8'));previous=json.loads(args.spatial_proof.read_text(encoding='utf-8'))
    if previous['verdict']!='measured_spatial_budget_and_uncertainty_no_independent_physical_validation':raise ValueError('Invalid previous study')
    if sha256(root/previous['freeze_path'])!=previous['freeze_sha256']:raise ValueError('Changed previous freeze')
    catalog=json.loads(args.bridge_proof.read_text(encoding='utf-8'));selection=authenticated_selection(root,args.pilot_proof)
    used={c['source']['sha256'] for pair in selection for c in pair['captures']}|{c['source']['sha256'] for c in previous['captures']}
    extras={}
    for iso in (1600,51200):
        candidates=[r for r in inventory['records'] if r['category']=='confirmed_dark' and r['split']=='development'
            and r['metadata']['iso']==iso and r['metadata']['model']=='Canon EOS R6 Mark III' and r['sha256'] not in used]
        candidates.sort(key=lambda r:r['original_path'])
        if not candidates:raise ValueError('No distinct third calibration read without opening reserve')
        extras[iso]=candidates[0]
        if sha256(extras[iso]['path'])!=extras[iso]['sha256']:raise ValueError('Changed additional calibration RAW')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/study_spectral_budget.py','tests/raw_bench/spectral_read_model.py','tests/raw_bench/study_spatial_budget.py',
             'tests/raw_bench/camera_noise_budget.py','tests/raw_bench/transport_batch.py','tests/raw_bench/cfa_transport_probe.cpp','src/raw/Demosaic.cpp']
    freeze=dict(schema_version=1,created_utc=stamp,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={str(path):sha256(path) for path in (args.inventory,args.pilot_proof,args.bridge_proof,args.spatial_proof)},
        extras=extras,selection=selection,real_controls=previous['captures'],tools=dict(transport=sha256(args.transport_tool),unpack=sha256(args.unpack_tool)),
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        protocol=dict(calibration_seeds=CALIBRATION,control_seeds=CONTROL,guide_seed=GUIDE,domains=DOMAINS,patterns=PATTERNS,rois=ROIS,
            model='4x4 CFA cross spectral density from THREE physical calibration darks, three frozen spatial tiles each (nine tiles, NOT nine acquisitions); per-phase DC removed; amplitude normalized to central frozen temporal b.',
            spectral_noise='Independent real Gaussian white four-phase field -> Hermitian spectral square root -> real CFA; independent each draw. Periodic128 phase grid.',
            real_replay='Cycle the two already-inspected new-to-transport control read fields by seed parity. 32 draws are not32 real acquisitions.',
            gate='Unchanged .15 flat median / every sigma .8..1.25 / transition improves flat; evaluate domains SEPARATELY. No retuning.',
            numerical_PSD='Only roundoff-negative eigenvalues within1e-10 spectral maximum set to zero, count/tolerance saved; genuine negative spectrum refused.'),
        limits=['Adaptive continuation after gate failure; previous controls now consumed. No fresh physical holdout is claimed.',
            'Internal spectral simulation shares read-model and signal law with predictor; can verify implementation/stability, NOT sensor truth.',
            'Stationary Gaussian shape, three same-session darks, constant drift removed; no physical fixed-pattern or hot-pixel model.',
            'Shape uses spatial dark structure, amplitude uses temporal variance; transfer to physical correlated read noise remains to validate.',
            'Real replay contains only TWO physical control reads per ISO, reused and previously inspected; distinct random photons do not multiply acquisitions.',
            'No denoising/renders/DCP/LCP or production integration/resource qualification. Reserved final data remain sealed.'])
    frozen=root/'docs/validation/raw'/f'spectral-budget-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as f:json.dump(freeze,f,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,data_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),freeze_sha256=sha256(frozen),models=[],captures=[],rows=[])
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(d.resolve()) for d in args.runtime_dir)+os.pathsep+env.get('PATH','')
    destination=root/'build-patch_raw_bench'/f'spectral-budget-{stamp}';destination.mkdir();started=time.monotonic()
    try:
        for pair in selection:
            iso=int(pair['label'].split('ISO')[1]);manifest=json.loads(Path(pair['captures'][0]['path']).read_text(encoding='utf-8'))
            context=prepare_context(catalog,sha256(args.bridge_proof),manifest['metadata']['model'],iso,manifest['applied'])
            reads=[tile for c in pair['captures'] for tile in calibration_tiles(c['path'],context,c['source']['sha256'])]
            extra=extras[iso];path=destination/f'{iso}-extra.json'
            process=subprocess.run([str(args.unpack_tool.resolve()),'--input',extra['path'],'--output',str(path),'--mode','parity'],capture_output=True,text=True,env=env,timeout=60)
            if process.returncode:raise RuntimeError(process.stderr)
            _,extra_manifest=read_residual(path,context,extra['sha256']);reads+=calibration_tiles(path,context,extra['sha256'])
            result['captures'].append(dict(path=str(path),sha256=sha256(path),source=extra_manifest['source']))
            model=prepare(reads,context);result['models'].append(dict(iso=iso,context=context,physical_calibration_frames=3,summary=model['summary'],root_sha256=hashlib.sha256(model['root'].tobytes()).hexdigest()))
            control_records=[r for r in previous['captures'] if r['metadata']['iso']==iso]
            if len(control_records)!=2:raise ValueError('Need exact two control reads')
            actual_reads=[read_residual(r['path'],context,r['source']['sha256'])[0] for r in control_records]
            population=lambda seed:draw(model,seed,iso)
            for domain in DOMAINS:
                control_read=population if domain=='spectral_internal' else lambda seed:actual_reads[seed%2]
                for distribution in ('gaussian','scaled_poisson'):
                    for pattern in PATTERNS:
                        nominal=mosaic(fixture(iso,pattern))
                        sample=simulate_context(context,nominal,control_read(GUIDE),np.random.default_rng(np.random.SeedSequence([GUIDE,iso])),distribution)
                        observed=native_batch(args.transport_tool,[sample],env)[0][0]
                        guide=cv2.boxFilter(observed,-1,(31,31),normalize=True,borderType=cv2.BORDER_REFLECT_101)
                        controls=ensemble(args.transport_tool,nominal,control_read,context,CONTROL,distribution,env)
                        spatial=ensemble(args.transport_tool,mosaic(guide),population,context,CALIBRATION,distribution,env)
                        diagnostic=ensemble(args.transport_tool,nominal,population,context,CALIBRATION,distribution,env)
                        regions={}
                        for name,roi in ROIS.items():
                            mean=local_mean(observed,roi)
                            flat=ensemble(args.transport_tool,mosaic(np.broadcast_to(mean,(256,256,3))),population,context,CALIBRATION,distribution,env,{name:roi})
                            regions[name]=dict(control_covariances=controls['covariances'][name],models={label:dict(
                                calibration_covariances=values['covariances'][name],comparison=compare_draws(values['covariances'][name],controls['covariances'][name]))
                                for label,values in (('flat',flat),('spatial',spatial),('clean_diagnostic',diagnostic))})
                        result['rows'].append(dict(iso=iso,domain=domain,distribution=distribution,pattern=pattern,regions=regions,control_identities=controls['identities']))
                        print(iso,domain,distribution,pattern,len(result['rows']),'/32',flush=True)
        if len(result['rows'])!=32:raise ValueError('Incomplete population design')
        result['gates']=gates(result['rows']);result['verdict']='measured_population_budget_domains_separate_not_physical_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'spectral-budget-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':raise SystemExit(main())
