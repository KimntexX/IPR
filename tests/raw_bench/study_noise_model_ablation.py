"""Same filter/force, texture-contaminated versus noise-only models; bench-only ablation."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

os.environ['OPENBLAS_NUM_THREADS'] = '1'
os.environ['OMP_NUM_THREADS'] = '1'
import numpy as np
from audit_dark_mean import sha256
from generate_noise_oracle import instrument
from study_structure_prediction import authenticated_selection, load_intensity
from study_structure_amplitude_free import template


VARIANTS = dict(actual=('noisy_100','clean_100'),
    blind_pooled=('blind_pooled_noisy','blind_pooled_clean'),
    same_noise=('same_noise_pooled_noisy','same_noise_pooled_clean_stress'),
    independent_noise=('independent_noise_pooled_noisy','independent_noise_pooled_clean_stress'))


def encoded(field):
    pixels = np.repeat(field[...,None],3,axis=2).astype('<f4').tobytes()
    return base64.b64encode(pixels).decode('ascii'),hashlib.sha256(pixels).hexdigest()


def invoke(tool,payload,env):
    run = subprocess.run([str(tool.resolve())],input=json.dumps(payload)+'\n',
        capture_output=True,text=True,encoding='utf-8',env=env,timeout=60)
    if run.returncode:
        raise RuntimeError(f'Native model ablation failed: {run.stderr}')
    return json.loads(run.stdout)


def summarize(rows):
    output = []
    for key in sorted({(r['noise_mode'],r['pair'],r['pattern']) for r in rows}):
        group = [r for r in rows if (r['noise_mode'],r['pair'],r['pattern']) == key]
        summary = dict(noise_mode=key[0],pair=key[1],pattern=key[2],cases=len(group),variants={})
        for name,(noisy,clean) in VARIANTS.items():
            errors = [r['measurements'][noisy]['rgb_mse'] for r in group]
            gains = [r['measurements'][clean]['pattern_gain'] for r in group
                     if r['measurements'][clean]['pattern_gain'] is not None]
            summary['variants'][name] = dict(median_mse=float(np.median(errors)),
                median_mse_ratio_to_actual=float(np.median([error/r['measurements']['noisy_100']['rgb_mse'] for error,r in zip(errors,group)])),
                median_mse_ratio_to_blind_pooled=float(np.median([error/r['measurements']['blind_pooled_noisy']['rgb_mse'] for error,r in zip(errors,group)])),
                median_clean_gain=float(np.median(gains)) if gains else None)
        output.append(summary)
    return output


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--parent-proof',type=Path,required=True)
    p.add_argument('--pilot-proof',type=Path,required=True)
    p.add_argument('--original-tool',type=Path,required=True)
    p.add_argument('--oracle-tool',type=Path,required=True)
    p.add_argument('--generated-source',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[2]
    parent = json.loads(args.parent_proof.read_text(encoding='utf-8'))
    if parent['verdict'] != 'measured_stress_only_no_candidate_filter_or_witness_certification' or len(parent['rows']) != 64:
        raise ValueError('Incomplete stress parent')
    parent_freeze = root/parent['freeze_path']
    if sha256(parent_freeze) != parent['freeze_sha256']:
        raise ValueError('Changed parent freeze')
    context = json.loads(parent_freeze.read_text(encoding='utf-8'))
    for name in ('tests/raw_bench/study_structure_amplitude_free.py','src/raw/CameraDenoise.cpp','src/raw/CameraDenoise.h'):
        if context['sources'][name]['sha256'] != sha256(root/name):
            raise ValueError(f'Parent recipe/engine changed: {name}')
    if args.generated_source.read_text(encoding='utf-8') != instrument((root/'src/raw/CameraDenoise.cpp').read_text(encoding='utf-8')):
        raise ValueError('Generated boundary does not match current source and exact hook')
    selection = authenticated_selection(root,args.pilot_proof)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    sources = ['tests/raw_bench/study_noise_model_ablation.py','tests/raw_bench/generate_noise_oracle.py',
        'tests/raw_bench/noise_bench_control.h','tests/raw_bench/noise_bench_control.cpp',
        'tests/raw_bench/structure_baseline_probe.cpp','src/raw/CameraDenoise.cpp','src/raw/CameraDenoise.h',
        'tests/raw_bench/CMakeLists.txt']
    freeze = dict(schema_version=1,created_utc=stamp,study='constant_noise_model_ablation',
        revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),
        parent_proof_sha256=sha256(args.parent_proof),pilot_sha256=sha256(args.pilot_proof),pairs=selection,
        tools={name:dict(path=str(path),sha256=sha256(path)) for name,path in
               (('original',args.original_tool),('instrumented',args.oracle_tool))},
        generated_source=dict(sha256=sha256(args.generated_source),text=args.generated_source.read_text(encoding='utf-8')),
        sources={s:dict(sha256=sha256(root/s),text=(root/s).read_text(encoding='utf-8')) for s in sources},
        protocol=dict(cases='Exact64 parent cases and bit-identical source float32 payloads',settings=[100,100],
            variants=VARIANTS,point='After estimate(), before measured/noise filtering',
            parity='Original target versus generated Bypass, every output pixel, noisy and clean, all64 cases',
            zero='Explicit zero model on clean must return exact input pixels',
            noise_only='Same current pooled MAD/covariance estimated from injected noise alone; not exact physical sigma oracle',
            independent='Other development black sameISO, same source noise scale, no amplitude fit to the answer',
            source_noise_only='float32 observed-truth+.03, rounding retained',
            clean_stress='Same nonzero noise budget on clean structure; stress response, not true noiseless oracle',
            decision='Isolate routing and noise estimation; no strength tuning, no production promotion from this ablation.'),
        limits=['Generated diagnostic artifact only; actual engine source/public interface unchanged.',
            'Observer sees model BEFORE override; actual frozen model is pooled state captured from the named noise-only run.',
            'Known noise still estimated by current robust estimator; this is not exact covariance or an optimal denoising bound.',
            'Achromatic synthetic signal plus actual postRCD dark residual, no photons/CFA signal dependence/DCP.',
            'Four already-used development frames and same cases; no intact validation or photo/latency qualification.'])
    frozen = root/'docs/validation/raw'/f'noise-model-ablation-freeze-v001-{stamp}.json'
    with frozen.open('x',encoding='utf-8') as handle:
        json.dump(freeze,handle,ensure_ascii=False,indent=2,allow_nan=False)
    result = dict(schema_version=1,created_utc=stamp,freeze_path=str(frozen.relative_to(root)),
        freeze_sha256=sha256(frozen),rows=[],contracts=dict(pixel_parities=0,zero_identities=0,parent_payloads=0))
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(str(v.resolve()) for v in args.runtime_dir)+os.pathsep+env.get('PATH','')
    start = time.monotonic()
    try:
        fields = {pair['label']:[load_intensity(c)[0] for c in pair['captures']] for pair in selection}
        for index,row in enumerate(parent['rows']):
            truth = template(row['pattern'],row['amplitude']).astype(np.float32).astype(float)
            perturbation = fields[row['pair']][row['shot']]*row['source_noise_scale']
            observed = (truth+perturbation).astype(np.float32).astype(float)
            reference = .03+fields[row['pair']][1-row['shot']]*row['source_noise_scale']
            payload = {};hashes={}
            for name,field in (('observed',observed),('truth',truth),('reference',reference)):
                payload[name],hashes[name] = encoded(field)
            if any(hashes[name] != row['current_filter']['input_sha256'][name] for name in ('observed','truth')):
                raise ValueError('Parent pixels changed')
            result['contracts']['parent_payloads'] += 1
            original = invoke(args.original_tool,payload,env)
            measured = invoke(args.oracle_tool,payload,env)
            for name in ('noisy_100','clean_100'):
                if original[name]['pixels_sha256'] != measured[name]['pixels_sha256']:
                    raise ValueError('Bypass pixel parity failed; ablation invalid')
                for metric in ('rgb_mse','pattern_gain','models'):
                    if original[name][metric] != row['current_filter'][name][metric]:
                        raise ValueError(f'Original parent metric changed: {metric}')
                result['contracts']['pixel_parities'] += 1
            if measured['zero_model_clean']['pixels_sha256'] != hashes['truth']:
                raise ValueError('Zero model identity failed')
            result['contracts']['zero_identities'] += 1
            result['rows'].append(dict(noise_mode=row['noise_mode'],pair=row['pair'],shot=row['shot'],
                pattern=row['pattern'],amplitude=row['amplitude'],source_noise_scale=row['source_noise_scale'],
                input_sha256=hashes,measurements=measured))
            if (index+1)%8 == 0:
                print(index+1,'/64 model cases qualified',flush=True)
        result['summary'] = summarize(result['rows'])
        result['verdict'] = 'measured_model_ablation_only_no_production_noise_model'
    except Exception as exc:
        result['verdict'] = 'fail'
        result['error'] = f'{type(exc).__name__}: {exc}'
    result['seconds'] = time.monotonic()-start
    output = root/'docs/validation/raw'/f'noise-model-ablation-v001-{stamp}.json'
    with output.open('x',encoding='utf-8') as handle:
        json.dump(result,handle,ensure_ascii=False,indent=2,allow_nan=False)
    print(output,result['verdict'],result.get('error',''),flush=True)
    return int(result['verdict']=='fail')


if __name__ == '__main__':
    raise SystemExit(main())
