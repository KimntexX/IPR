"""Typed curve adapter to the unchanged known development evaluator, no new cohort."""
import argparse
from datetime import datetime,timezone
import json
from pathlib import Path
import shutil
import time

import torch
from campaign_artifacts import sha256
from memory_budget import MemoryGuard
from neural_inference_contract import check_inference
from run_noisy_moment_pilot import evaluate,runtime_versions
from run_edge_protected_probe import PROBE_TOOLS
from edge_protected_noise import EdgeProtectedNoisyUNet
from local_moment_unet import LocalMomentUNet
from training_resume import validate_state


def eligible_curve(proof,seed):
    return (proof.get('status')=='completed_sensor_curve_continuation_not_quality_qualification'
        and proof.get('seed')==seed and proof.get('preflight') is False
        and proof.get('completed_steps')==2048 and proof.get('config',{}).get('steps')==2048
        and proof.get('config',{}).get('scope')=='cfa_front'
        and proof.get('frozen_reconstruction_bit_exact') is True
        and len(proof.get('losses',[]))==2048
        and sum(proof.get('exposure_counts',{}).values())==8192)


def prepare(root):
    build=root/'build-patch_neural_restoration';output=build/'curve-known-development-plan-v001'
    old_path=build/'repeatability-control-protected-v001/result.json'
    old=json.loads(old_path.read_text(encoding='utf-8'))
    data_path=Path(old['plan']['data_plan_path']);data=json.loads(data_path.read_text(encoding='utf-8'))
    if sha256(data_path)!=old['plan']['data_plan_sha256'] or data['runtime']!=runtime_versions():raise ValueError('Known development data/runtime changed')
    check_inference(data,old['plan']['tools'])
    if any(sha256(Path(__file__).with_name(n))!=old['plan']['tools'][n] for n in PROBE_TOOLS):raise ValueError('Known evaluator implementation changed')
    curve=build/'sensor-learning-campaign-v001';progress=json.loads((curve/'progress.json').read_text(encoding='utf-8'))
    if progress['status']!='completed_bounded_sensor_curve_known_only':raise ValueError('Complete curve required')
    photo_path=curve/'photo-plans/step2048/plan.json';photos=json.loads(photo_path.read_text(encoding='utf-8'))
    output.mkdir(exist_ok=False);refs=[]
    for seed in (71,73):
        source=curve/f'seed{seed}/result.json';proof=json.loads(source.read_text(encoding='utf-8'))
        if not eligible_curve(proof,seed):raise ValueError('Incomplete or differently scoped curve')
        record=next(r for r in photos['models'] if r['seed']==seed)
        weight=Path(record['path']);checkpoint=curve/f'seed{seed}/checkpoint-002048.pt'
        if sha256(source)!=record['result_sha256'] or sha256(weight)!=record['weights_sha256'] or sha256(checkpoint)!=record['checkpoint_sha256']:
            raise ValueError('Curve lineage changed')
        state=torch.load(checkpoint,weights_only=True,map_location='cpu');validate_state(state,state['context'])
        exported=torch.load(weight,weights_only=True,map_location='cpu')['state_dict']
        if state['completed_step']!=2048 or any(not torch.equal(v,exported[n]) for n,v in state['model_state'].items()):raise ValueError('Export does not match final checkpoint')
        folder=output/f'seed{seed}';folder.mkdir()
        shutil.copyfile(weight,folder/'weights.pt');shutil.copyfile(source,folder/'result.json')
        refs.append(dict(seed=seed,folder=str(folder.resolve()),weights_sha256=sha256(folder/'weights.pt'),result_sha256=sha256(folder/'result.json')))
    plan=dict(schema_version=1,frozen_at_utc=datetime.now(timezone.utc).isoformat(),data_plan_path=str(data_path.resolve()),
        data_plan_sha256=sha256(data_path),models=refs,memory_policy=data['memory_policy'],
        tools={n:sha256(Path(__file__).with_name(n)) for n in (*PROBE_TOOLS,'qualify_sensor_curve.py')},
        initial_development_proof=str(old_path),initial_development_sha256=sha256(old_path),
        training_curve_sha256=sha256(curve/'progress.json'),photo_model_manifest_sha256=sha256(photo_path),
        policy='Known development observations/parents/criteria unchanged. Typed2048step curve source; no claim of an old512step training recipe. Both final seeds, no selection; zero fitting steps here.',
        limits=['Procedural development controls, not independent confirmation or photographic qualification.',
                'Historic learned reconstruction baseline, not production RCD; same as the original criteria.',
                'Failure stops qualification; no new reserved contents or eligibility flag invented.'])
    with (output/'plan.json').open('x',encoding='utf-8') as stream:json.dump(plan,stream,indent=2)
    return output/'plan.json'


def run(root,plan_path):
    build=root/'build-patch_neural_restoration';output=build/'curve-known-development-v001';output.mkdir(exist_ok=False)
    plan=json.loads(plan_path.read_text(encoding='utf-8'))
    if any(sha256(Path(__file__).with_name(n))!=h for n,h in plan['tools'].items()):raise ValueError('Frozen development adapter changed')
    data_path=Path(plan['data_plan_path']);data=json.loads(data_path.read_text(encoding='utf-8'))
    if sha256(data_path)!=plan['data_plan_sha256']:raise ValueError('Known data changed')
    source_path=Path(data['source'])/'result.json'
    if sha256(source_path)!=data['source_result_sha256']:raise ValueError('Historical source changed')
    source=json.loads(source_path.read_text(encoding='utf-8'))
    old=json.loads(Path(plan['initial_development_proof']).read_text(encoding='utf-8'))
    result=dict(schema_version=1,status='incomplete',measured_at_utc=datetime.now(timezone.utc).isoformat(),
        plan=plan,plan_sha256=sha256(plan_path),models=[],scope='Known development, not new reserved data')
    started=time.monotonic();guard=MemoryGuard(**plan['memory_policy'])
    try:
        with guard:
            torch.set_num_threads(4);torch.set_num_interop_threads(1);torch.use_deterministic_algorithms(True)
            for ref in plan['models']:
                guard.check();folder=Path(ref['folder'])
                if sha256(folder/'weights.pt')!=ref['weights_sha256'] or sha256(folder/'result.json')!=ref['result_sha256']:raise ValueError('Curve export changed')
                weights=torch.load(folder/'weights.pt',weights_only=True,map_location='cpu')['state_dict']
                model=EdgeProtectedNoisyUNet(data['config']['width']).eval().requires_grad_(False);model.load_state_dict(weights,strict=True)
                p=next(p for p in data['parents'] if p['seed']==ref['seed']);path=Path(p['folder'])/'weights.pt'
                if sha256(path)!=p['weights_sha256']:raise ValueError('Original parent changed')
                parent=LocalMomentUNet(data['config']['width']).eval().requires_grad_(False)
                parent.load_state_dict(torch.load(path,weights_only=True,map_location='cpu')['state_dict'],strict=True)
                measured=evaluate(model,parent,data,source,guard)
                prior=next(r for r in old['models'] if r['seed']==ref['seed'])
                identity=lambda r:(r['target_sha256'],r['noisy_sha256'],r['sigma_sha256'],r['phase_index'],r['noise_index'],r['output_sha256']['parent'])
                if [identity(r) for r in measured['records']]!=[identity(r) for r in prior['records']]:raise ValueError('Known observations/parent replay changed')
                if any(not torch.equal(v,model.state_dict()[n]) for n,v in weights.items()):raise ValueError('Weights modified by evaluation')
                measured.update(seed=ref['seed'],source_result_sha256=ref['result_sha256'],learned_weights_sha256=ref['weights_sha256'],
                    fitting_steps=0,known_observations_and_parent_bit_exact=True)
                result['models'].append(measured)
                (output/f'seed{ref["seed"]}.json').write_text(json.dumps(measured,indent=2)+'\n',encoding='utf-8')
                print('KNOWN DEVELOPMENT',ref['seed'],'gates',measured['pilot_gates_passed'],flush=True)
                del model,parent,weights
            result['pilot_gates_passed']=all(r['pilot_gates_passed'] for r in result['models'])
            result['status']='completed_fixed_edge_protection_probe_not_photographic_qualification'
            if any(sha256(Path(__file__).with_name(n))!=h for n,h in plan['tools'].items()):raise ValueError('Code changed during evaluation')
    except Exception as error:
        result.update(status='stopped_on_error',error=str(error));raise
    finally:
        result.update(memory=guard.stats(),elapsed_seconds=time.monotonic()-started)
        (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        stamp=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
        with (root/'docs/validation/raw'/f'neural-curve-known-development-v001-{stamp}.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepare',action='store_true');parser.add_argument('--plan',type=Path)
    args=parser.parse_args();root=Path(__file__).resolve().parents[2]
    if args.prepare:print(prepare(root))
    elif args.plan is not None:run(root,args.plan)
    else:parser.error('--prepare or --plan required')
