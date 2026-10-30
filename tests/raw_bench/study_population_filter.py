"""Frozen same-filter qualification of population covariance; no engine edits."""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time

os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
import cv2
import numpy as np
from audit_dark_mean import sha256
from camera_noise_budget import mosaic, simulate_context
from generate_noise_oracle import instrument
from spectral_read_model import prepare, draw
from study_spectral_budget import calibration_tiles, DOMAINS
from study_spatial_budget import read_residual, ensemble, LEVELS
from study_signal_filter import encoded_rgb
from study_noise_model_ablation import invoke
from transport_batch import native_batch

PATTERNS = ('flat', 'weak_luma', 'weak_color', 'curved_edge')
CALIBRATION = tuple(range(170001, 170033))
CONTROLS = (190001, 190002)
GUIDE = 151001
ROI = (144, 144, 96, 96)


def fixture(iso, pattern):
    low, high = LEVELS[iso]
    center = (low + high) / 2
    y, x = np.mgrid[:256, :256]
    bump = np.exp(-((x-188)**2+(y-185)**2)/(2*13**2))
    detail = .6*np.sin((x+.23*y)/3.7)*np.exp(-((x-190)**2+(y-190)**2)/(2*40**2)) + bump
    amplitude = (high-low)*.10
    if pattern == 'flat': rgb = np.full((256,256,3), center)
    elif pattern == 'weak_luma': rgb = np.repeat((center+amplitude*detail)[...,None],3,axis=2)
    elif pattern == 'weak_color': rgb = center+amplitude*detail[...,None]*np.array([1.,-.25,-.5])
    elif pattern == 'curved_edge':
        edge = np.tanh((x-185-.002*(y-180)**2)/2.5)
        rgb = center+.3*(high-low)*edge[...,None]*np.array([1.,.75,.5])
    else: raise ValueError('Unknown pattern')
    return rgb.astype(np.float32).astype(float)


def gates(rows):
    result = {}
    for domain in DOMAINS:
        result[domain] = {}
        group = [r for r in rows if r['domain'] == domain]
        if not group: raise ValueError('Missing domain')
        for candidate in ('spectral', 'bank'):
            ratios = [r['measurements'][candidate]['external_covariance_noisy']['rgb_mse'] /
                      r['measurements'][candidate]['noisy_100']['rgb_mse'] for r in group]
            detail_losses = [r['measurements'][candidate]['clean_100']['pattern_gain']-
                             r['measurements'][candidate]['external_covariance_clean_stress']['pattern_gain']
                             for r in group if r['pattern'] != 'flat']
            bias_excess = [float(np.linalg.norm(r['measurements'][candidate]['external_covariance_noisy']['opponent_bias']))-
                           float(np.linalg.norm(r['measurements'][candidate]['noisy_100']['opponent_bias']))
                           for r in group]
            # Dimensionless mean-color bias allowance relative to input RGB RMS.
            bias_ratios = [b/max(np.sqrt(r['input_rgb_mse']),1e-30) for b,r in zip(bias_excess,group)]
            chroma_ratios = [sum(r['measurements'][candidate]['external_covariance_noisy']['opponent_mse'][1:]) /
                            max(sum(r['measurements'][candidate]['noisy_100']['opponent_mse'][1:]),1e-30) for r in group]
            checks = dict(median_mse_better=bool(np.median(ratios)<1),
                          p95_mse_le_110=bool(np.percentile(ratios,95)<=1.10),
                          worst_mse_le_125=bool(max(ratios)<=1.25),
                          worst_chroma_mse_le_125=bool(max(chroma_ratios)<=1.25),
                          clean_gain_loss_le_002=bool(max(detail_losses)<=.02),
                          mean_color_bias_excess_le_010_input_rms=bool(max(bias_ratios)<=.10))
            result[domain][candidate] = dict(checks=checks,passed=all(checks.values()),
                mse_ratio_p50_p95_max=np.percentile(ratios,[50,95,100]).tolist(),
                chroma_mse_ratio_p50_p95_max=np.percentile(chroma_ratios,[50,95,100]).tolist(),
                worst_clean_gain_loss=max(detail_losses),worst_bias_excess_input_rms=max(bias_ratios))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('parent','transport-tool','original-tool','oracle-tool','generated-source'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args = parser.parse_args(); cv2.setNumThreads(1)
    root = Path(__file__).resolve().parents[2]
    parent = json.loads(args.parent.read_text(encoding='utf-8'))
    frozen_parent = root/parent['freeze_path']
    if sha256(frozen_parent)!=parent['freeze_sha256']: raise ValueError('Changed parent freeze')
    previous = json.loads(frozen_parent.read_text(encoding='utf-8'))
    for name in ('src/raw/Demosaic.cpp',):
        if sha256(root/name)!=previous['sources'][name]['sha256']: raise ValueError('Changed transport')
    if args.generated_source.read_text(encoding='utf-8') != instrument((root/'src/raw/CameraDenoise.cpp').read_text(encoding='utf-8')):
        raise ValueError('Instrumented source mismatch')
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources = ['tests/raw_bench/'+s for s in ('study_population_filter.py','spectral_read_model.py','study_spatial_budget.py',
        'camera_noise_budget.py','transport_batch.py','structure_baseline_probe.cpp','noise_bench_control.cpp','generate_noise_oracle.py')]
    sources += ['src/raw/CameraDenoise.cpp','src/raw/Demosaic.cpp']
    freeze = dict(schema_version=1,created_utc=stamp,revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent=dict(path=str(args.parent),sha256=sha256(args.parent)),parent_freeze_sha256=sha256(frozen_parent),
        tools={name:sha256(path) for name,path in (('transport',args.transport_tool),('original',args.original_tool),('oracle',args.oracle_tool))},
        sources={name:dict(sha256=sha256(root/name),text=(root/name).read_text(encoding='utf-8')) for name in sources},
        protocol=dict(patterns=PATTERNS,calibration=CALIBRATION,controls=CONTROLS,guide=GUIDE,roi=ROI,settings=[100,100],
            domains=DOMAINS,expected_cases=64,bank='Same three physical calibration frames, central crop, cycled by seed modulo3; 32 draws per profile, like spectral.',
            candidate='Observed spatial box31 guide; covariance averaged on exact filter ROI, constant profile injected into unchanged five-scale filter. Not a per-pixel budget.',
            gates='Per domain and candidate: median MSE ratio<1, p95<=1.10, worst<=1.25; worst chroma MSE ratio<=1.25; clean-stress gain loss<=.02 vs current; opponent mean-bias excess<=.10 input RGB RMS. No retuning.'),
        limits=['Adaptive study after prior results; motifs and seeds new, not independent physical validation.',
            'Real replay reuses two already-seen black frames per ISO; independent photon draws do not multiply acquisitions.',
            'Same empirical law generates and predicts shot noise; internal spectral domain is self-model consistency, not truth.',
            'Clean stress applies a nonzero noisy-image budget to clean truth; diagnostic, not a realistic zero-noise model.',
            'Fixed100/100, one ROI, same session R6III1600/51200, no DCP/LCP or real-photo quality claim. Final reserve sealed.'])
    frozen = root/'docs/validation/raw'/f'population-filter-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as f: json.dump(freeze,f,ensure_ascii=False,indent=2,allow_nan=False)
    result = dict(schema_version=1,data_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),freeze_sha256=sha256(frozen),rows=[],contracts=dict(parities=0,zero_identities=0))
    env = os.environ.copy(); env['PATH'] = os.pathsep.join(str(p.resolve()) for p in args.runtime_dir)+os.pathsep+env.get('PATH','')
    started = time.monotonic()
    try:
        for entry in parent['models']:
            iso=entry['iso']; context=entry['context']; pair=next(p for p in previous['selection'] if p['label'].endswith(str(iso)))
            paths=[(c['path'],c['source']['sha256']) for c in pair['captures']]
            extra=next(c for c in parent['captures'] if c['source']['sha256']==previous['extras'][str(iso)]['sha256'])
            paths.append((extra['path'],extra['source']['sha256']))
            tiles=[tile for path,digest in paths for tile in calibration_tiles(path,context,digest)]
            model=prepare(tiles,context)
            bank_reads=[read_residual(path,context,digest)[0] for path,digest in paths]
            physical=[read_residual(c['path'],context,c['source']['sha256'])[0] for c in previous['real_controls'] if c['metadata']['iso']==iso]
            population=lambda seed: draw(model,seed,iso)
            bank=lambda seed: bank_reads[seed%3]
            for domain in DOMAINS:
                read=population if domain=='spectral_internal' else lambda seed:physical[seed%2]
                for distribution in ('gaussian','scaled_poisson'):
                    for pattern in PATTERNS:
                        target=mosaic(fixture(iso,pattern)); truth=native_batch(args.transport_tool,[target],env)[0][0]
                        sample=simulate_context(context,target,read(GUIDE),np.random.default_rng(np.random.SeedSequence([GUIDE,iso])),distribution)
                        guide_observed=native_batch(args.transport_tool,[sample],env)[0][0]
                        guide=mosaic(cv2.boxFilter(guide_observed,-1,(31,31),normalize=True,borderType=cv2.BORDER_REFLECT_101))
                        profiles={name:ensemble(args.transport_tool,guide,provider,context,CALIBRATION,distribution,env,{'filter':ROI})
                                  for name,provider in (('spectral',population),('bank',bank))}
                        covariances={name:np.mean(value['covariances']['filter'],axis=0).tolist() for name,value in profiles.items()}
                        for seed in CONTROLS:
                            sample=simulate_context(context,target,read(seed),np.random.default_rng(np.random.SeedSequence([seed,iso])),distribution)
                            image,identity=native_batch(args.transport_tool,[sample],env)[0]
                            payload={}; hashes={}
                            for name,array in (('observed',image),('truth',truth),('reference',np.full((256,256,3),.03))):
                                payload[name],hashes[name]=encoded_rgb(array)
                            original=invoke(args.original_tool,payload,env); measurements={}
                            for candidate,covariance in covariances.items():
                                measured=invoke(args.oracle_tool,dict(payload,covariance_profile=covariance),env)
                                for key in ('noisy_100','clean_100'):
                                    if measured[key]['pixels_sha256']!=original[key]['pixels_sha256']:raise ValueError('Filter parity failed')
                                    result['contracts']['parities']+=1
                                if measured['zero_model_clean']['pixels_sha256']!=hashes['truth']:raise ValueError('Zero identity failed')
                                result['contracts']['zero_identities']+=1
                                measurements[candidate]=measured
                            result['rows'].append(dict(iso=iso,domain=domain,distribution=distribution,pattern=pattern,seed=seed,
                                input_sha256=hashes,transport=identity,covariance_profiles=covariances,
                                calibration_covariances={name:value['covariances']['filter'] for name,value in profiles.items()},
                                input_rgb_mse=float(np.mean((image[144:240,144:240]-truth[144:240,144:240])**2)),measurements=measurements))
                        print(iso,domain,distribution,pattern,len(result['rows']),'/64',flush=True)
        if len(result['rows'])!=64:raise ValueError('Incomplete case grid')
        result['gates']=gates(result['rows']);result['verdict']='measured_same_filter_population_budget_not_production_validation'
    except Exception as exc:
        result['verdict']='fail';result['error']=f'{type(exc).__name__}: {exc}'
    result['seconds']=time.monotonic()-started
    output=root/'docs/validation/raw'/f'population-filter-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__=='__main__':raise SystemExit(main())
