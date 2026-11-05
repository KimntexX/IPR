"""Localize known fine failures before RGB reconstruction, no fitting or new contents."""
from datetime import datetime,timezone
import json
from pathlib import Path
import time
import torch
from torch.nn import functional as F
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from confirm_fine_noise import fine_signal,observation
from context_data import extend_observation,crop_centre
from bayer_canonical import canonicalize,spatial_reflect,predict
from edge_protected_noise import EdgeProtectedNoisyUNet,edge_confidence
from noisy_moment_unet import NoisyMomentUNet
from run_noisy_moment_pilot import NOISE
from run_synthetic_joint_pilot import tensor_hash
from joint_bayer_unet import sample_rgb
from synthetic_bayer_data import PATTERNS
from sensor_transfer_math import site_response


def run():
    root=Path(__file__).resolve().parents[2];build=root/'build-patch_neural_restoration'
    path=build/'curve-known-fine-v001/result.json';fine=json.loads(path.read_text(encoding='utf-8'))
    if fine['status']!='completed_fine_signal_regression_not_photographic_qualification' or fine['pilot_gates_passed']:
        raise ValueError('Complete failed known regression required')
    initial_path=build/'repeatability-control-protected-v001/result.json';initial=json.loads(initial_path.read_text(encoding='utf-8'))
    plan=fine['plan'];fields=[f for f in plan['fields'] if plan['cases'][f['case_index']]['amplitude']==plan['criteria']['strong_amplitude'] and f['noise_index']>0]
    if len(fields)!=864:raise ValueError('All strong known observations required, without failure selection')
    output=build/'curve-sensor-transfer-v001';output.mkdir(exist_ok=False)
    intention=dict(schema_version=1,created_at_utc=datetime.now(timezone.utc).isoformat(),fine_sha256=sha256(path),
        initial_proof_sha256=sha256(initial_path),fields=fields,cases=plan['cases'],models=plan['source']['models'],
        policy='All48strong groups, bothseeds; observed CFA/initial protected/final protected/final unprotected diagnostic. Same observed/sigma/context hashes, no training or new model candidate.',
        scope='Known failure localization, not qualification. G1/G2 separate; sensor response compared with existing fullRGB group response.',
        tools={n:sha256(Path(__file__).with_name(n)) for n in ('diagnose_curve_sensor_transfer.py','sensor_transfer_math.py','edge_protected_noise.py','noisy_moment_unet.py')})
    with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(intention,stream,indent=2)
    result=dict(schema_version=1,status='started',plan=intention,records=[],bridges=[])
    guard=MemoryGuard(**plan['source']['data']['memory_policy']);started=time.monotonic()
    try:
        with guard,torch.inference_mode():
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            for ref in plan['source']['models']:
                path=Path(ref['folder'])/'weights.pt'
                if sha256(path)!=ref['weights_sha256']:raise ValueError('Final weights changed')
                model=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                model.load_state_dict(torch.load(path,weights_only=True,map_location='cpu')['state_dict'],strict=True)
                prior=next(r for r in initial['plan']['models'] if r['seed']==ref['seed']);old_path=Path(prior['folder'])/'weights.pt'
                if sha256(old_path)!=prior['weights_sha256']:raise ValueError('Initial weights changed')
                old=EdgeProtectedNoisyUNet(8).eval().requires_grad_(False)
                old.load_state_dict(torch.load(old_path,weights_only=True,map_location='cpu')['state_dict'],strict=True)
                cached=None
                for index,field in enumerate(fields):
                    guard.check()
                    if time.monotonic()-started>1800:raise TimeoutError('Bounded30minute diagnostic reached')
                    case=plan['cases'][field['case_index']]
                    if cached!=field['case_index']:
                        truth,wave,cosine=fine_signal(case);expanded,_,_=fine_signal(case,384,-96);cached=field['case_index']
                        if tensor_hash(truth)!=case['target_sha256']:raise ValueError('Known target changed')
                    tags=torch.tensor([PATTERNS[field['phase_index']]])
                    clean,observed,sigma=observation(truth,tags,field)
                    if tensor_hash(observed)!=field['observation_sha256'] or tensor_hash(sigma)!=field['sigma_sha256']:raise ValueError('Known noisy inputs changed')
                    values,deviations=extend_observation(expanded,tags,observed,sigma,*NOISE[field['noise_index']],210000000+field['noise_seed'])
                    canonical,noise,labels,flags=canonicalize(values/2,deviations/2,tags)
                    def restore(packed):return crop_centre(F.pixel_unshuffle(spatial_reflect(F.pixel_shuffle(packed,2),flags)*2,2),96)
                    corrected=restore(model.correct_observations(canonical,noise,labels))
                    older=restore(old.correct_observations(canonical,noise,labels))
                    unprotected=restore(NoisyMomentUNet.correct_observations(model,canonical,noise,labels))
                    confidence=crop_centre(edge_confidence(canonical,noise),96)[:,:,32:-32,32:-32]
                    if index==0:
                        full=crop_centre(predict(model,values/2,deviations/2,tags,False,True)*2,192)
                        delta=float((sample_rgb(full,tags)-corrected).abs().max())
                        if delta>2e-6:raise ValueError('Corrected-site and fullRGB bridge differs')
                        result['bridges'].append(dict(seed=ref['seed'],max_error=delta))
                    result['records'].append(dict(**field,seed=ref['seed'],colour_indices=tags[0].tolist(),
                        observed=site_response(observed,clean,wave,cosine),initial=site_response(older,clean,wave,cosine),
                        final=site_response(corrected,clean,wave,cosine),unprotected=site_response(unprotected,clean,wave,cosine),
                        confidence_mean=float(confidence.mean()),confidence_below01_fraction=float((confidence<.1).float().mean())))
                    if (index+1)%144==0:print('SENSOR DIAG',ref['seed'],index+1,'/',len(fields),flush=True)
                del model,old
            if len(result['records'])!=1728:raise ValueError('Incomplete strong-group diagnosis')
            groups=[]
            for source_group in fine['groups']:
                if source_group['amplitude']!=plan['criteria']['strong_amplitude']:continue
                selected=[r for r in result['records'] if r['seed']==source_group['seed'] and r['noise_index']==source_group['noise_index']
                    and all(plan['cases'][r['case_index']][k]==source_group[k] for k in ('frequency','chromatic'))]
                by_colour={}
                for colour in range(4):
                    by_colour[colour]={name:sum(row[name][row['colour_indices'].index(colour)]['sine_gain'] for row in selected)/len(selected)
                        for name in ('observed','initial','final','unprotected')}
                groups.append(dict(seed=source_group['seed'],noise_index=source_group['noise_index'],frequency=source_group['frequency'],
                    chromatic=source_group['chromatic'],cases=len(selected),sensor_gain_by_colour=by_colour,
                    fullRGB_gain_over_parent=source_group['gain_over_parent'],existing_checks=source_group['strong_checks']))
            result.update(status='completed_known_sensor_transfer_diagnosis_not_qualification',groups=groups)
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(memory=guard.stats(),elapsed_seconds=time.monotonic()-started)
        with (output/'result.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-curve-sensor-transfer-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2,allow_nan=False)


if __name__=='__main__':run()
