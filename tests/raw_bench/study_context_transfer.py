"""Frozen camera-context budget and transfer to local levels, colors, and edges.

Research simulation only; no denoising, DCP, real-photo qualification or engine edit.
"""
import argparse
import copy
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
from audit_signal_noise_bridge import PHASES
from capture import load_capture
from study_structure_prediction import authenticated_selection
from study_cfa_signal_transport import native,compare_covariance,LEVELS,CALIBRATION_SEEDS
from camera_noise_budget import prepare_context,mosaic,simulate_context,local_mean,prepare_profile,profile_key,band_covariances

GUIDE_SEED=1393
CONTROL_SEEDS=(1613,1833,2053)
PATTERNS=('neutral','warm','cool','tonal_step','color_step','colored_texture')
ROIS={'left':(96,32,64,64),'edge':(96,96,64,64),'right':(96,160,64,64)}
DATA_VERSION=2


def fixture(iso,pattern):
    if pattern not in PATTERNS:raise ValueError('Unknown fixture')
    low,high=LEVELS[iso];_,x=np.mgrid[:256,:256]
    warm=np.array([1.4,1.,.6]);cool=np.array([.6,1.,1.4])
    if pattern=='neutral':rgb=np.full((256,256,3),low)
    elif pattern in ('warm','cool'):rgb=np.broadcast_to(low*(warm if pattern=='warm' else cool),(256,256,3)).copy()
    elif pattern=='tonal_step':rgb=np.repeat(np.where(x<128,low,high)[...,None],3,axis=2)
    elif pattern=='color_step':rgb=low*np.where((x<128)[...,None],warm,cool)
    else:
        noise=np.random.default_rng(74042).normal(size=(256,256))
        texture=cv2.GaussianBlur(noise,(0,0),.8,borderType=cv2.BORDER_REFLECT_101)
        texture=np.tanh(texture/texture.std())
        rgb=(low+high)/2+min(.0015,.2*low)*texture[...,None]*np.array([1.,-.25,-.5])
    return rgb.astype(np.float32).astype(float)


def contexts(catalog,digest,manifest):
    applied=manifest['applied'];camera=manifest['metadata']['model'];iso=manifest['metadata']['iso']
    variants={}
    variants['metadata']=copy.deepcopy(applied)
    wb=copy.deepcopy(applied);wb['gains'][0]*=1.2;wb['gains'][2]*=.8
    variants['WB_witness']=wb
    normalization=copy.deepcopy(applied);normalization['black']=[v+4 for v in applied['black']];normalization['divisor']=10000
    variants['normalization_witness']=normalization
    return {name:prepare_context(catalog,digest,camera,iso,values) for name,values in variants.items()}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bridge-proof',type=Path,required=True)
    p.add_argument('--pilot-proof',type=Path,required=True)
    p.add_argument('--transport-tool',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args=p.parse_args();cv2.setNumThreads(1)
    root=Path(__file__).resolve().parents[2];catalog=json.loads(args.bridge_proof.read_text(encoding='utf-8'))
    if sha256(root/catalog['freeze_path'])!=catalog['freeze_sha256']:raise ValueError('Changed catalog freeze')
    selection=authenticated_selection(root,args.pilot_proof)
    stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources=['tests/raw_bench/study_context_transfer.py','tests/raw_bench/camera_noise_budget.py',
             'tests/raw_bench/study_cfa_signal_transport.py','tests/raw_bench/cfa_transport_probe.cpp',
             'src/raw/Demosaic.cpp','src/raw/Demosaic.h']
    freeze=dict(schema_version=1,created_utc=stamp,study='camera_context_noise_transfer',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        catalog_sha256=sha256(args.bridge_proof),pilot_sha256=sha256(args.pilot_proof),selection=selection,
        tool_sha256=sha256(args.transport_tool),numpy=np.__version__,opencv=cv2.__version__,
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        protocol=dict(patterns=PATTERNS,rois_yxhw=ROIS,levels=LEVELS,guide_seed=GUIDE_SEED,
            control_seeds=CONTROL_SEEDS,calibration_seeds=CALIBRATION_SEEDS,
            distributions=['gaussian','scaled_poisson'],context_variants=['metadata','WB_witness','normalization_witness'],
            local_signal='31x31 mean guide from first observed RGB only, then ROI mean; no clean target or residual used to prepare budget.',
            covariance='Independent other-dark simulated flat, four draws, five B3 covariance matrices at SAME geometry/ROI as measurement.',
            geometry_control='Version2 isolates image-border/support effects from color/level transfer; no threshold or seed retuning.',
            reference_center='Frozen central-region development black per phase, not per-shot recentering.',
            latent_floor='Nonnegative physical latent mean for variance only; signed input/output pixels never clamped.',
            decision='Expose per-axis errors and extrapolations; do not retune guide/ROIs/thresholds or promote a uniform profile on mean errors.'),
        limits=['R6 Mark III characterized only; transport uses two ISO and four reused development darks.',
            'WB/normalization variants are synthetic mathematical witnesses, not physical calibrations of other cameras or modes.',
            'Three control seeds exclude guide/calibration seeds, but share the same actual dark residual; not independent acquisitions.',
            'Bright ISO1600 and some colored signals extrapolate the fit; all phase flags retained.',
            'Local flat proxy can fail near edges or varying levels, especially coarse scales; no per-pixel model or production cache delivered.',
            'No denoising, DCP/LCP, real-photo rendering or production resource qualification in this study.'])
    frozen=root/'docs/validation/raw'/f'context-transfer-freeze-v{DATA_VERSION:03d}-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as f:json.dump(freeze,f,ensure_ascii=False,indent=2,allow_nan=False)
    result=dict(schema_version=1,data_version=DATA_VERSION,campaign_id=f'context-transfer-v{DATA_VERSION:03d}-{stamp}',created_utc=stamp,
                freeze_path=str(frozen.relative_to(root)),freeze_sha256=sha256(frozen),contexts=[],rows=[])
    env=os.environ.copy();env['PATH']=os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started=time.monotonic()
    try:
        for pair in selection:
            reads=[];manifests=[]
            for capture in pair['captures']:
                manifest,stages=load_capture(capture['path']);manifests.append(manifest)
                keys=sorted([k for k in stages if k[0]=='unpacked_cfa'],key=lambda k:k[1][1]);descriptor,dn=stages[keys[1]]
                if descriptor['context']['cfa']!=[[0,1],[3,2]] or manifest['mode']!='signed':raise ValueError('Unexpected CFA capture')
                context=prepare_context(catalog,sha256(args.bridge_proof),manifest['metadata']['model'],manifest['metadata']['iso'],manifest['applied'])
                residual=dn[...,0].astype(float)
                for phase,(dy,dx,_) in PHASES.items():residual[dy::2,dx::2]-=context['phases'][phase]['reference_black']
                reads.append(residual)
            if manifests[0]['applied']!=manifests[1]['applied'] or manifests[0]['metadata']['iso']!=manifests[1]['metadata']['iso']:
                raise ValueError('Pair normalization/ISO mismatch')
            variants=contexts(catalog,sha256(args.bridge_proof),manifests[0]);iso=manifests[0]['metadata']['iso']
            result['contexts'].append(dict(iso=iso,variants=variants))
            for context_name,context in variants.items():
                for distribution in ('gaussian','scaled_poisson'):
                    for pattern in PATTERNS:
                        nominal=mosaic(fixture(iso,pattern));clean,_=native(args.transport_tool,nominal,env)
                        for shot in range(2):
                            transported=[];identities=[]
                            for seed in (GUIDE_SEED,)+CONTROL_SEEDS:
                                rng=np.random.default_rng(np.random.SeedSequence([seed,shot,iso]))
                                sample=simulate_context(context,nominal,reads[shot],rng,distribution)
                                output,identity=native(args.transport_tool,sample,env)
                                transported.append(output['after_median']);identities.append(identity)
                            entries={}
                            for roi_name,roi in ROIS.items():
                                mean=local_mean(transported[0],roi)
                                seeds=[s+10000*(1-shot) for s in CALIBRATION_SEEDS]
                                profile=prepare_profile(context,mean,reads[1-shot],args.transport_tool,env,seeds,distribution,roi)
                                profile['key']=profile_key(context,mean,hashlib.sha256(reads[1-shot].tobytes()).hexdigest(),freeze['tool_sha256'],seeds,distribution,roi)
                                observed=np.mean([band_covariances(v-clean['after_median'],roi) for v in transported[1:]],axis=0)
                                predicted=np.array(profile['covariance'])
                                entries[roi_name]=dict(profile=profile,comparisons=[compare_covariance(a,b) for a,b in zip(predicted,observed)],
                                    guide_error_to_clean_roi=(mean-local_mean(clean['after_median'],roi)).tolist())
                            result['rows'].append(dict(iso=iso,context=context_name,distribution=distribution,pattern=pattern,shot=shot,
                                control_identities=identities,regions=entries))
                        print(iso,context_name,distribution,pattern,len(result['rows']),'/144',flush=True)
        if len(result['rows'])!=144:raise ValueError('Incomplete fixed design')
        result['verdict']='measured_contextual_budget_transfer_not_photo_or_production_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'context-transfer-v{DATA_VERSION:03d}-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':raise SystemExit(main())
