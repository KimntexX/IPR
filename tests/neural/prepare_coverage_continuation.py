"""Freeze fresh-random vs stratified texture continuation, then engineering checks."""
import argparse
import copy
from datetime import datetime,timezone
import json
from pathlib import Path

import torch

from audit_texture_coverage import coverage
from continuation_initialization import initialize_continuation
from diagnose_train_learnability import digest,save
from memory_budget import MemoryGuard
from multiscale_correction import composed_support
from noisy_moment_unet import NoisyMomentUNet,FORWARD_TOOLS
from noisy_training_policy import configure_trainable
from prepare_warm_continuation import prefix_source,check_embedding
from run_noisy_moment_pilot import train,TOOLS,MEMORY_POLICY,runtime_versions
from run_synthetic_joint_pilot import tensor_hash
from texture_sampling import sample_events


def prepare(root,output):
    base=root/'build-patch_neural_restoration'
    if not output.resolve().is_relative_to(base.resolve()):
        raise ValueError('Dedicated neural output required')
    template_path=base/'paired-correlation-plan-v002/control/plan.json'
    template=json.loads(template_path.read_text(encoding='utf-8'))
    source_path=Path(template['source'])/'result.json'
    if digest(source_path)!=template['source_result_sha256']:
        raise ValueError('Original replay source changed')
    engineering_path=base/'correlation-engineering-v001/result.json'
    engineering=json.loads(engineering_path.read_text(encoding='utf-8'))
    if (engineering['status']!='passed_multiscale_engineering_preflight_not_quality_qualification'
            or engineering['support']!=composed_support() or engineering['runtime']!=runtime_versions()
            or any(digest(Path(__file__).with_name(n))!=engineering['tools'][n] for n in FORWARD_TOOLS)):
        raise ValueError('Current inference engineering required')
    source=json.loads(source_path.read_text(encoding='utf-8'))
    now=datetime.now(timezone.utc)
    identity='neural-coverage-preflight-v001-'+now.strftime('%Y%m%dT%H%M%SZ')
    result=dict(schema_version=1,campaign_id=identity,measured_at_utc=now.isoformat(),status='incomplete',
        template_sha256=digest(template_path),source_sha256=digest(source_path),
        script_sha256=digest(Path(__file__)),architecture_engineering_sha256=digest(engineering_path),
        cases=[],embedding_checks=[],coverage={},sampling={},
        policy='Both arms learn the same observed correlation branch from identical2048-step initializers, fresh Adam0.00025,512steps, fixed losses and replayed nontexture exposures. Only fresh texture sampling differs. No scores used to select data or weights.')
    output.mkdir(parents=True,exist_ok=False)
    guard=MemoryGuard(**MEMORY_POLICY)
    try:
        with guard:
            torch.set_num_threads(4)
            torch.set_num_interop_threads(1)
            torch.use_deterministic_algorithms(True)
            excluded=set(template['lineage_exclusions']['target_sha256'])
            excluded.update(e['target_sha256'] for e in template['edges']+template['mixed'])
            # Only hashes of already-consulted targets; never generate a reserved cohort.
            known=base/'fixed-point-fine-plan-v001/plan.json'
            known_plan=json.loads(known.read_text(encoding='utf-8'))
            excluded.update(c['target_sha256'] for c in known_plan['cases'])
            events={}
            for arm in ('control','stratified'):
                rows,selection=sample_events(template['texture_augmentation']['events'],arm,excluded,guard,tensor_hash)
                events[arm]=rows
                result['sampling'][arm]=selection
                result['coverage'][arm]=coverage(rows)
                excluded.update(e['target_sha256'] for e in rows)
                print('DATA',arm,len(rows),'missing',result['coverage'][arm]['missing_cells'],flush=True)
            if result['coverage']['stratified']['missing_cells']:
                raise ValueError('Stratified positive joint coverage incomplete')
            original_source_path=Path(source['origin']['path'])
            if digest(original_source_path)!=source['origin']['sha256']:
                raise ValueError('Ancestor source changed')
            original_source=json.loads(original_source_path.read_text(encoding='utf-8'))
            small=prefix_source(original_source,8)
            for arm in ('control','stratified'):
                for ref in template['warm_start']['models']:
                    guard.check()
                    torch.manual_seed(ref['seed'])
                    parent=next(r for r in template['parents'] if r['seed']==ref['seed'])
                    path=Path(parent['folder'])/'weights.pt'
                    if digest(path)!=parent['weights_sha256']:
                        raise ValueError('Frozen reconstruction changed')
                    clean=torch.load(path,weights_only=True)['state_dict']
                    model=NoisyMomentUNet(8)
                    initialize_continuation(root,model,clean,ref)
                    configure_trainable(model,'cfa_front',True)
                    if arm=='control':
                        result['embedding_checks'].extend(check_embedding(model,template,ref,guard))
                    config=dict(template['config'],steps=8,learning_rate=.00025,statistics_trainable=True)
                    context=dict(plan_sha256='coverage-engineering-'+digest(template_path),seed=ref['seed'],
                        config=config,parent_weights_sha256=ref['weights_sha256'],source_result_sha256=digest(original_source_path))
                    progress={}
                    losses,counts=train(model,small,config,guard,output/f'engineering-{arm}-{ref["seed"]}.pt',
                        context,progress,8,texture_events=[e for e in events[arm] if e['serial']<32])
                    if any(not torch.equal(v,model.state_dict()[n]) for n,v in clean.items()):
                        raise ValueError('Frozen reconstruction changed in preflight')
                    if float(model.noise_features.statistics.weight.detach().norm())==0.:
                        raise ValueError('Correlation branch did not learn in preflight')
                    result['cases'].append(dict(arm=arm,seed=ref['seed'],completed_steps=len(losses),
                        exposures=sum(counts.values()),final_loss=losses[-1],progress=progress))
                    print('ENGINEERING',arm,ref['seed'],flush=True)
                    del model,clean
            result['lineage_exclusions']=sorted(excluded)
            result['status']='passed_fresh_vs_stratified_engineering_not_quality_qualification'
    except Exception as error:
        result.update(status='failed_coverage_preparation',error=str(error))
        raise
    finally:
        result['memory']=guard.stats()
        save(output/'preflight.json',result)
        save(root/'docs/validation/raw'/f'{identity}.json',result)
    tools={n:digest(Path(__file__).with_name(n)) for n in TOOLS+('prepare_coverage_continuation.py',)}
    for arm in ('control','stratified'):
        plan=copy.deepcopy(template)
        plan.update(frozen_at_utc=datetime.now(timezone.utc).isoformat(),tools=tools,
            engineering_preflight=dict(path=str((output/'preflight.json').resolve()),sha256=digest(output/'preflight.json'),campaign_id=identity),
            continuation=dict(arm=arm,experiment='fresh_random_vs_stratified_texture_coverage',stage_steps=512,total_optimizer_steps=2560,
                data='Same nontexture replay, source schedule, noise IDs and both initializers. Fresh disjoint texture ID streams; original generator distribution constrained only by candidate quotas.'),
            training_policy=result['policy'])
        plan['config']['statistics_trainable']=True
        plan['texture_augmentation'].update(events=events[arm],sampling=result['sampling'][arm],
            coverage=result['coverage'][arm],
            policy_scope='Base generator parameter law; sampling quotas/rejection recorded separately. Not a claim of identical empirical marginals between arms.')
        plan['lineage_exclusions']['target_sha256']=result['lineage_exclusions']
        plan['lineage_exclusions']['scope']='All initializer contents, all fresh training contents of both arms and consulted fine truths; no reserved generation.'
        plan['conditioning']='Same40base features and32observed correlation cues, both branches trainable in both arms.'
        (output/arm).mkdir()
        save(output/arm/'plan.json',plan)
    print('PREPARED',output,flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    prepare(Path(__file__).resolve().parents[2],args.output)
