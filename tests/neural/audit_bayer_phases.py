"""Cross all four Bayer phases with the same20 RGB fields; fixed model, no fit."""
import argparse
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import time

import torch

from diagnose_train_learnability import digest,save
from joint_bayer_unet import JointBayerUNet,bilinear,sample_rgb
from run_synthetic_joint_pilot import metrics,aggregate,tensor_hash
from synthetic_bayer_data import PATTERNS,FAMILIES,signal


PHASE_NAMES=('RGGB','GRBG','GBRG','BGGR')


def phase_diagnostics(outputs,baselines,target):
    if len(outputs)!=4 or len(baselines)!=4 or min(target.shape[-2:])<=96:
        raise ValueError('Four phase results with matching core support required')
    if any(v.shape!=target.shape or not torch.isfinite(v).all() for v in outputs+baselines):
        raise ValueError('Finite matching phase tensors required')
    crop=(slice(None),slice(None),slice(48,-48),slice(48,-48))
    pred=torch.stack([v[crop] for v in outputs]);base=torch.stack([v[crop] for v in baselines]);truth=target[crop][None]
    return dict(output_phase_variance=float(pred.var(dim=0,unbiased=False).mean()),
        bilinear_phase_variance=float(base.var(dim=0,unbiased=False).mean()),
        output_mse_per_phase=(pred-truth).square().mean(dim=(1,2,3,4)).tolist(),
        bilinear_mse_per_phase=(base-truth).square().mean(dim=(1,2,3,4)).tolist(),
        output_bias_rgb_per_phase=(pred-truth).mean(dim=(1,3,4)).tolist())


def run(parent,output):
    root=Path(__file__).resolve().parents[2];parent,output=parent.resolve(),output.resolve()
    if not all(p.is_relative_to((root/'build-patch_neural_restoration').resolve()) for p in (parent,output)):
        raise ValueError('Use dedicated neural build')
    proof=json.loads((parent/'result.json').read_text(encoding='utf-8'))
    if (proof['status']!='completed_synthetic_reconstruction_stage_not_photographic_qualification'
            or not proof['freeze']['config']['exact_observations']):raise ValueError('Expected completed consistent reconstruction')
    config=proof['freeze']['config'];weights=parent/'synthetic-joint-weights.pt'
    if digest(weights)!=proof['weights_sha256']:raise ValueError('Changed fixed model')
    for n in ('joint_bayer_unet.py','synthetic_bayer_data.py'):
        if digest(Path(__file__).with_name(n))!=proof['freeze']['tools'][n]:raise ValueError('Changed model/signal implementation')
    cases=proof['freeze']['evaluation_cases']
    if len(cases)!=20 or len({(c['family'],c['seed']) for c in cases})!=20 or any(c['noise_index']!=0 for c in cases):
        raise ValueError('Expected same20 distinct noise-free RGB contents')
    if any(sum(c['family']==family for c in cases)!=4 for family in FAMILIES):
        raise ValueError('Expected four frozen contents per signal family')
    now=datetime.now(timezone.utc);campaign='neural-bayer-phase-audit-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    freeze=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        git_sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(parent),result_sha256=digest(parent/'result.json'),weights_sha256=digest(weights)),
        config=config,contents=cases,phase_names=list(PHASE_NAMES),patterns=[list(p) for p in PATTERNS],
        policy='Same RGB truth in all4 phases, no added noise, same model/scale/core48; no model or phase selection.',
        tools={n:digest(Path(__file__).with_name(n)) for n in ('audit_bayer_phases.py','joint_bayer_unet.py',
            'synthetic_bayer_data.py','run_synthetic_joint_pilot.py')})
    output.mkdir(parents=True,exist_ok=False);save(output/'freeze.json',freeze)
    threads=min(4,max(1,(os.cpu_count() or 1)-2))
    torch.set_num_threads(threads);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
    model=JointBayerUNet(config['width'])
    checkpoint=torch.load(weights,map_location='cpu',weights_only=True)
    if checkpoint['freeze_sha256']!=proof['freeze_sha256']:raise ValueError('Checkpoint provenance differs')
    model.load_state_dict(checkpoint['state_dict'],strict=True);model.requires_grad_(False);model.eval()
    rows=[];content_rows=[];started=time.monotonic()
    result=dict(schema_version=1,data_version=1,campaign_id=campaign,measured_at_utc=now.isoformat(),
        freeze=freeze,freeze_sha256=digest(output/'freeze.json'),records=rows,contents=content_rows,status='incomplete',
        threads_intra=threads,versions=dict(torch=torch.__version__),
        limits=['Existing procedural contents already consulted; not independent photo validation.',
            'Different CFA phases measure different sites; bilinear variation is the physical sampling witness.',
            'Output-phase variance includes legitimate observation changes; not pure learned artifact by itself.',
            'No added noise, training, parameter search, chosen best phase or production change.'])
    try:
        with torch.inference_mode():
            for case in cases:
                target=signal(case['family'],case['seed'],config['side'])[None]
                target_hash=tensor_hash(target);outputs=[];bases=[]
                for phase,pattern_values in enumerate(PATTERNS):
                    pattern=torch.tensor([pattern_values],dtype=torch.int64)
                    packed=sample_rgb(target,pattern);packed_hash=tensor_hash(packed);sigma=torch.zeros_like(packed)
                    prediction=model(packed/config['scale'],sigma,pattern,exact_observations=True)*config['scale']
                    base=bilinear(packed,pattern)
                    if not torch.equal(sample_rgb(prediction,pattern),packed):raise ValueError('Observed values differ')
                    if tensor_hash(packed)!=packed_hash:raise ValueError('Source modified')
                    measured=metrics(target,base,prediction)
                    row=dict(family=case['family'],seed=case['seed'],phase_index=phase,phase_name=PHASE_NAMES[phase],
                        pattern=list(pattern_values),target_sha256=target_hash,packed_sha256=packed_hash,
                        output_sha256=tensor_hash(prediction),observed_site_exact=True,metrics=measured)
                    rows.append(row);outputs.append(prediction);bases.append(base)
                    if list(pattern_values)==case['pattern']:
                        old=[r for r in proof['records'] if r['family']==case['family'] and r['seed']==case['seed']]
                        if len(old)!=1 or old[0]['target_sha256']!=target_hash or old[0]['noisy_sha256']!=packed_hash:
                            raise ValueError('Original phase input identity differs')
                        expected=old[0]['metrics']['output_sse']
                        if abs(expected-measured['output_sse'])>1e-6*max(1e-8,expected):raise ValueError('Original phase replay differs')
                if tensor_hash(target)!=target_hash:raise ValueError('Truth modified')
                content_rows.append(dict(family=case['family'],seed=case['seed'],target_sha256=target_hash,
                    **phase_diagnostics(outputs,bases,target)))
        result['summary']=aggregate(rows)
        result['by_phase']={name:aggregate([r for r in rows if r['phase_name']==name]) for name in PHASE_NAMES}
        result['by_family']={family:aggregate([r for r in rows if r['family']==family]) for family in FAMILIES}
        result['by_family_phase']={family:{name:aggregate([r for r in rows if r['family']==family and r['phase_name']==name])
            for name in PHASE_NAMES} for family in FAMILIES}
        result['phase_variance_by_family']={family:{name:sum(r[name] for r in content_rows if r['family']==family)/4
            for name in ('output_phase_variance','bilinear_phase_variance')} for family in FAMILIES}
        if digest(weights)!=freeze['parent']['weights_sha256']:raise ValueError('Model changed')
        if any(digest(Path(__file__).with_name(n))!=v for n,v in freeze['tools'].items()):raise ValueError('Study code changed')
        result['status']='completed_crossed_bayer_phase_audit_no_training'
    except Exception as error:
        result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        save(output/'result.json',result);save(root/'docs/validation/raw'/f'{campaign}.json',result)
    print(json.dumps(result['by_phase'],indent=2),flush=True)
    print('COMPLETE '+campaign,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();run(a.parent,a.output)
