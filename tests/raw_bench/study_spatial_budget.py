"""Frozen uncertainty/selection audit and spatial-noise-budget replication.

Fresh synthetic seeds/fixtures and new-to-transport DEVELOPMENT black frames.
Not an independent physical sensor validation: those frames informed the law.
"""
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
from audit_signal_noise_bridge import PHASES
from capture import load_capture
from camera_noise_budget import prepare_context,mosaic,simulate_context,local_mean
from study_structure_prediction import authenticated_selection
from study_cfa_signal_transport import native,compare_covariance
from transport_batch import native_batch

CALIBRATION_SEEDS=tuple(range(70001,70033))
CONTROL_SEEDS=tuple(range(90001,90033))
GUIDE_SEED=61001
PATTERNS=('flat','diagonal_luma','curved_ramp','diagonal_color')
ROIS={'left':(96,32,64,64),'edge':(96,96,64,64),'right':(96,160,64,64)}
LEVELS={1600:(.0015,.0032),51200:(.02,.09)}


def fixture(iso,pattern):
    low,high=LEVELS[iso];y,x=np.mgrid[:256,:256];mask=x+.65*y>210
    if pattern=='flat':rgb=np.full((256,256,3),(low+high)/2)
    elif pattern=='diagonal_luma':rgb=np.repeat(np.where(mask,high,low)[...,None],3,axis=2)
    elif pattern=='curved_ramp':
        value=low+(high-low)*(x/255.)**2
        rgb=value[...,None]*np.stack([1+.08*np.sin(y/23.),np.ones_like(value),1-.08*np.sin(y/23.)],axis=-1)
    elif pattern=='diagonal_color':rgb=(low+high)/2*np.where(mask[...,None],np.array([1.15,1.,.8]),np.array([.8,1.,1.2]))
    else:raise ValueError('Unknown new fixture')
    return rgb.astype(np.float32).astype(float)


def choose_controls(inventory,selection):
    old={capture['source']['sha256'] for pair in selection for capture in pair['captures']}
    output={}
    for iso in LEVELS:
        eligible=[r for r in inventory['records'] if r['category']=='confirmed_dark' and r['split']=='development'
            and r['metadata']['iso']==iso and r['metadata']['model']=='Canon EOS R6 Mark III'
            and r['sha256'] not in old and r['status']=='available_hash_match']
        eligible.sort(key=lambda r:r['original_path'])
        if len(eligible)<2:raise ValueError('Insufficient explicit development-only transfer controls')
        output[iso]=eligible[:2]
    return output


def read_residual(path,context,expected_hash):
    manifest,stages=load_capture(path)
    if manifest['source']['sha256']!=expected_hash or manifest['metadata']['iso']!=context['iso']:raise ValueError('Capture source/ISO mismatch')
    keys=sorted([k for k in stages if k[0]=='unpacked_cfa'],key=lambda k:k[1][1]);descriptor,dn=stages[keys[1]]
    if descriptor['context']['cfa']!=[[0,1],[3,2]] or dn.shape!=(256,256,1):raise ValueError('Unexpected crop')
    residual=dn[...,0].astype(float)
    for phase,(dy,dx,_) in PHASES.items():residual[dy::2,dx::2]-=context['phases'][phase]['reference_black']
    return residual,manifest


def covariance_fields(residual,rois):
    current=residual.astype(float)@np.array([[.25,.5,.25],[.5,0,-.5],[-.5,1,-.5]]).T
    output={name:[] for name in rois}
    for level in range(5):
        step=1<<level;kernel=np.zeros(4*step+1);kernel[::step]=np.array([1,4,6,4,1])/16
        base=cv2.sepFilter2D(current,-1,kernel,kernel,borderType=cv2.BORDER_REFLECT_101);detail=current-base
        for name,(y,x,h,w) in rois.items():output[name].append(np.cov(detail[y:y+h,x:x+w].reshape(-1,3),rowvar=False,ddof=1).tolist())
        current=base
    return output


def ensemble(tool,nominal,read,context,seeds,distribution,env,rois=ROIS):
    clean=native_batch(tool,[nominal],env)[0][0]
    profiles={name:[] for name in rois};means={name:[] for name in rois};identities=[]
    sum_residual=np.zeros((256,256,3))
    for start in range(0,len(seeds),8):
        arrays=[simulate_context(context,nominal,read(seed) if callable(read) else read,np.random.default_rng(np.random.SeedSequence([seed,context['iso']])),distribution)
                for seed in seeds[start:start+8]]
        for image,identity in native_batch(tool,arrays,env):
            residual=image-clean;identities.append(identity)
            sum_residual+=residual
            fields=covariance_fields(residual,rois)
            for name,roi in rois.items():
                profiles[name].append(fields[name])
                y,x,h,w=roi;means[name].append(residual[y:y+h,x:x+w].mean(axis=(0,1)).tolist())
    mean_field_cov=covariance_fields(sum_residual/len(seeds),rois)
    decomposition={}
    for name in rois:
        total=np.mean(profiles[name],axis=0)
        variable=(total-np.array(mean_field_cov[name]))*len(seeds)/(len(seeds)-1)
        decomposition[name]=dict(total_covariance=total.tolist(),variable_photon_conditional_covariance=variable.tolist(),
            common_conditional_covariance=(total-variable).tolist(),
            note=('Mean structure conditional on ONE frozen real read frame; not sensor FPN.' if not callable(read)
                  else 'Mean structure under the specified population read policy; not identified sensor FPN.')+' No PSD projection.')
    return dict(covariances=profiles,residual_means_rgb=means,identities=identities,decomposition=decomposition)


def compare_draws(predicted,observed):
    a=np.asarray(predicted);b=np.asarray(observed)
    if a.shape!=b.shape or a.shape!=(32,5,3,3):raise ValueError('Incomplete per-draw covariance')
    mean_a=a.mean(axis=0);mean_b=b.mean(axis=0)
    se=np.sqrt(a.var(axis=0,ddof=1)/len(a)+b.var(axis=0,ddof=1)/len(b))
    return [dict(**compare_covariance(x,y),mc_standard_error_covariance=z.tolist(),
        relative_mc_standard_error=float(np.linalg.norm(z)/max(np.linalg.norm(y),1e-30)))
        for x,y,z in zip(mean_a,mean_b,se)]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('inventory','pilot-proof','bridge-proof','transport-tool','unpack-tool'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();cv2.setNumThreads(1);root=Path(__file__).resolve().parents[2]
    inventory=json.loads(args.inventory.read_text(encoding='utf-8'));selection=authenticated_selection(root,args.pilot_proof)
    catalog=json.loads(args.bridge_proof.read_text(encoding='utf-8'));controls=choose_controls(inventory,selection)
    if sha256(root/catalog['freeze_path'])!=catalog['freeze_sha256']:raise ValueError('Changed characterization freeze')
    # Explicit inventory roles/hashes and deterministic name ordering BEFORE decode/evaluation.
    for group in controls.values():
        for record in group:
            if sha256(record['path'])!=record['sha256']:raise ValueError('Changed new control RAW')
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/study_spatial_budget.py','tests/raw_bench/camera_noise_budget.py',
        'tests/raw_bench/transport_batch.py','tests/raw_bench/cfa_transport_probe.cpp','src/raw/Demosaic.cpp','src/raw/Demosaic.h']
    frozen=root/'docs/validation/raw'/f'spatial-budget-freeze-v001-{stamp}.json'
    freeze=dict(schema_version=1,created_utc=stamp,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parents={str(path):sha256(path) for path in (args.inventory,args.pilot_proof,args.bridge_proof)},
        selection=selection,new_transfer_controls=controls,tools=dict(transport=sha256(args.transport_tool),unpack=sha256(args.unpack_tool)),
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        protocol=dict(patterns=PATTERNS,levels=LEVELS,rois=ROIS,calibration_seeds=CALIBRATION_SEEDS,control_seeds=CONTROL_SEEDS,
            guide_seed=GUIDE_SEED,distributions=['gaussian','scaled_poisson'],context='Original explicit pilot normalization only; no tuning.',
            candidates=['ROI-flat observed guide','spatial observed guide31','spatial clean diagnostic (privileged, not deployable)'],
            gate='Spatial candidate: flat covariance median error <=.15; all sigma ratios .8..1.25; transition median error lower than flat; no retuning if failed.',
            uncertainty='Per-draw 32 calibration+32 control covariances; descriptive independent-MC SE conditional on reused fixed read frames, NOT total confidence interval. Decompose common conditional field and variable photon term, not physical FPN identification.',
            same_read='Flat only: spatial clean diagnostic repeated with control read frame; privileged separates read-frame transfer from MC sampling.',
            bias='New motifs/seeds and new-to-transport development darks; not independent characterization/other-camera validation. No sealed validation RAW decoded.'),
        limits=['Same empirical a,b law generates and predicts; can test transfer implementation, not establish physical calibration truth.',
            'New control darks already contributed to black characterization, same camera/session; no pristine real-photo holdout claimed.',
            'Calibration/reference black patterns reused across seeds; 32 draws are not 32 real acquisitions.',
            'Spatial guide is postRCD noisy RGB, not a true CFA latent image. Its weak texture/noise ambiguity remains.',
            'No filtering, DCP/LCP or actual final photo quality evaluated; no production changes or runtime budget qualified.'])
    with frozen.open('x',encoding='utf-8') as f:json.dump(freeze,f,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,data_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),freeze_sha256=sha256(frozen),
        captures=[],rows=[],batch_pixel_parities=0)
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started=time.monotonic();capture_dir=root/'build-patch_raw_bench'/f'spatial-budget-{stamp}';capture_dir.mkdir()
    try:
        for pair in selection:
            iso=int(pair['label'].split('ISO')[1]);old_manifest=json.loads(Path(pair['captures'][0]['path']).read_text(encoding='utf-8'))
            context=prepare_context(catalog,sha256(args.bridge_proof),old_manifest['metadata']['model'],iso,old_manifest['applied'])
            refs=[read_residual(c['path'],context,c['source']['sha256'])[0] for c in pair['captures']]
            new_reads=[]
            for index,record in enumerate(controls[iso]):
                output=capture_dir/f'{iso}-{index}.json'
                run=subprocess.run([str(args.unpack_tool.resolve()),'--input',record['path'],'--output',str(output),'--mode','parity'],
                    env=env,capture_output=True,text=True,timeout=60)
                if run.returncode:raise RuntimeError(run.stderr)
                read,manifest=read_residual(output,context,record['sha256']);new_reads.append(read)
                result['captures'].append(dict(path=str(output),sha256=sha256(output),metadata=manifest['metadata'],source=manifest['source']))
            for distribution in ('gaussian','scaled_poisson'):
                for pattern in PATTERNS:
                    rgb=fixture(iso,pattern);nominal=mosaic(rgb)
                    # Batch refactor parity, independent of the noise model being scored.
                    one=native_batch(args.transport_tool,[nominal],env)[0]
                    scalar,identity=native(args.transport_tool,nominal,env)
                    if one[1]['output_sha256']!=identity['outputs_sha256']['after_median']:raise ValueError('Batch/scalar parity failed')
                    result['batch_pixel_parities']+=1
                    for shot in range(2):
                        sample=simulate_context(context,nominal,new_reads[shot],np.random.default_rng(np.random.SeedSequence([GUIDE_SEED,shot,iso])),distribution)
                        observed=native_batch(args.transport_tool,[sample],env)[0][0]
                        guide=cv2.boxFilter(observed,-1,(31,31),normalize=True,borderType=cv2.BORDER_REFLECT_101)
                        control=ensemble(args.transport_tool,nominal,new_reads[shot],context,CONTROL_SEEDS,distribution,env)
                        spatial=ensemble(args.transport_tool,mosaic(guide),refs[shot],context,CALIBRATION_SEEDS,distribution,env)
                        clean_diagnostic=ensemble(args.transport_tool,nominal,refs[shot],context,CALIBRATION_SEEDS,distribution,env)
                        same_read=ensemble(args.transport_tool,nominal,new_reads[shot],context,CALIBRATION_SEEDS,distribution,env) if pattern=='flat' else None
                        regions={}
                        for name,roi in ROIS.items():
                            mean=local_mean(observed,roi);flat_nominal=mosaic(np.broadcast_to(mean,(256,256,3)))
                            flat=ensemble(args.transport_tool,flat_nominal,refs[shot],context,CALIBRATION_SEEDS,distribution,env,{name:roi})
                            reference=control['covariances'][name]
                            regions[name]=dict(mean_rgb=mean.tolist(),control_covariances=reference,control_residual_means=control['residual_means_rgb'][name],
                                control_decomposition=control['decomposition'][name],
                                models={label:dict(calibration_covariances=values['covariances'][name],comparison=compare_draws(values['covariances'][name],reference),
                                                   decomposition=values['decomposition'][name])
                                    for label,values in (('flat',flat),('spatial',spatial),('clean_diagnostic',clean_diagnostic))})
                            if same_read:regions[name]['same_read_diagnostic']=dict(comparison=compare_draws(same_read['covariances'][name],reference),
                                decomposition=same_read['decomposition'][name])
                        result['rows'].append(dict(iso=iso,distribution=distribution,pattern=pattern,shot=shot,context=context,
                            control_identities=control['identities'],regions=regions))
                    print(iso,distribution,pattern,len(result['rows']),'/32',flush=True)
        if len(result['rows'])!=32 or len(result['captures'])!=4:raise ValueError('Incomplete frozen design')
        result['verdict']='measured_spatial_budget_and_uncertainty_no_independent_physical_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'spatial-budget-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':raise SystemExit(main())
